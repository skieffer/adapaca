/*
 * vim: ts=4 sw=4 et tw=0 wm=0
 *
 * libcola - A library providing force-directed network layout using the
 *           stress-majorization method subject to separation constraints.
 *
 * Copyright (C) 2006-2014  Monash University
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * See the file LICENSE.LGPL distributed with the library.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Author(s):  Tim Dwyer
 *             Michael Wybrow
 *             Steve Kieffer
*/

#include <algorithm>
#include <stdio.h>

#include "libvpsc/solve_VPSC.h"
#include "libvpsc/exceptions.h"
#include "libcola/cc_nonoverlapconstraints.h"
#include "libcola/aca3.h"

using namespace std;
using namespace vpsc;

namespace cola {

ACASepFlag negateSepFlag(ACASepFlag sf)
{
    unsigned short c = (unsigned short) sf;
    c += 16*c;
    c &= 60; // 00111100
    unsigned short b = c >> 2;
    ACASepFlag nf = (ACASepFlag) b;
    return nf;
}

ACAFlag sepToAlignFlag(ACASepFlag sf)
{
    return sf==ACANORTH || sf==ACASOUTH ? ACAVERT : ACAHORIZ;
}

ACAFlag perpAlignFlag(ACAFlag af)
{
    return af==ACAHORIZ ? ACAVERT : ACAHORIZ;
}

ACASepFlag vectorToSepFlag(double dx, double dy)
{
    int f = 0;
    f |= dx > 0 ? ACAEAST : dx < 0 ? ACAWEST : 0;
    f |= dy > 0 ? ACASOUTH : dy < 0 ? ACANORTH : 0;
    return (ACASepFlag) f;
}

// Create array of all four cardinals, in counterclockwise order,
// starting from the given one.
std::vector<ACASepFlag> ccwCardinalsFrom(ACASepFlag start)
{
    std::vector<ACASepFlag> ccw;
    int sn = start;
    ACASepFlag sf = start;
    for (int i = 0; i < 4; i++) {
        ccw.push_back(sf);
        sn = sn == 1 ? 8 : sn >> 1;
        sf = (ACASepFlag) sn;
    }
    return ccw;
}

// Create array of all four cardinals, in clockwise order,
// starting from the given one.
std::vector<ACASepFlag> cwCardinalsFrom(ACASepFlag start)
{
    std::vector<ACASepFlag> cw;
    int sn = start;
    ACASepFlag sf = start;
    for (int i = 0; i < 4; i++) {
        cw.push_back(sf);
        sn = sn == 8 ? 1 : sn << 1;
        sf = (ACASepFlag) sn;
    }
    return cw;
}

bool propsedSepConflictsWithExistingPosition(ACASepFlag pro, ACASepFlag ex)
{
    // Proposed separation pro conflicts with existing position ex
    // if the union of their bits contains both north and south, or
    // both east and west.
    int u = pro | ex;
    return ( ( (u&ACANORTHSOUTH) == ACANORTHSOUTH ) || ( (u&ACAEASTWEST) == ACAEASTWEST ) );
}

bool propsedSepConflictsWithExistingSepCo(ACASepFlag pro, ACASepFlag ex)
{
    // Proposed separation pro conflicts with existing separation constraint ex
    // if ex has any of the complementary bits of pro.
    int proComp = ACAALLSEP - pro;
    return (proComp & ex);
}

bool sortRootedEdges(const RootedEdge &lhs, const RootedEdge &rhs)
{
    return atan2(lhs.y,lhs.x) < atan2(rhs.y,rhs.x);
}

bool sortOrdAlignsByPenalty(const OrderedAlignment *lhs, const OrderedAlignment *rhs)
{
    return lhs->penalty < rhs->penalty;
}

ACALayout3::ACALayout3(
        const vpsc::Rectangles& rs,
        const std::vector<cola::Edge>& es,
        CompoundConstraints& ccs,
        const double idealLength,
        const bool preventOverlaps,
        const EdgeLengths& eLengths,
        TestConvergence *doneTest,
        PreIteration *preIteration)
    : m_n(rs.size()),
      m_m(es.size()),
      m_rs(rs),
      m_es(es),
      m_ccs(ccs),
      m_rc(NULL),
      m_idealLength(idealLength),
      m_preventOverlaps(preventOverlaps),
      m_edgeLengths(eLengths),
      m_doneTest(doneTest),
      m_preIteration(preIteration),
      m_nodePadding(0),
      m_addBendPointPenalty(true),
      m_favourLongEdges(false),
      m_postponeLeaves(true),
      m_useNonLeafDegree(true),
      m_allAtOnce(false),
      m_aggressiveOrdering(false),
      m_overlapPrevention(ACAOPCENTREALIGN),
      m_fdlayout(NULL),
      m_lengthUpperBound(1000000),
      m_didLayoutForLastAlignment(false),
      m_doFinalFDLayout(false),
      m_nocExemptions(new NonOverlapConstraintExemptions()),
      m_nocsInitialised(false),
      m_layoutPeriod(1000000)
{
    // By default, no edges are ignored.
    for (int i=0; i<m_m; i++) m_ignoreEdge.push_back(false);
    // By default, no nodes are ignored.
    for (int i=0; i<m_n; i++) m_ignoreNodeForOPWithOffsets.push_back(false);
    // Initialise tables.
    computeDegrees();
    generateVPSCConstraints();
    initStateTables();
}

ACALayout3::~ACALayout3(void)
{
    delete m_alignmentState;
    delete m_fdlayout;
    for (unsigned i = 0; i < m_ordAligns.size(); i++) {
        delete m_ordAligns.at(i);
    }
    for (unsigned i = 0; i < m_xvs.size(); i++) {
        delete m_xvs.at(i);
    }
    for (unsigned i = 0; i < m_yvs.size(); i++) {
        delete m_yvs.at(i);
    }
    // TODO: Check if there is anything else to clean up.
    // m_xcs for example?
}

void ACALayout3::createAlignments(void)
{
    if (!m_nocsInitialised) initNOCs();
    if (m_allAtOnce) {
        acaLoopAllAtOnce();
    } else {
        acaLoopOneByOne();
    }
}

bool ACALayout3::createOneAlignment(void)
{
    if (!m_nocsInitialised) initNOCs();
    return acaLoopOnce();
}

bool ACALayout3::applyOAsAllOrNothing(OrderedAlignments oas)
{
    if (!m_nocsInitialised) initNOCs();
    return allOrNothing(oas);
}

void ACALayout3::layout(void)
{
    if (!m_nocsInitialised) initNOCs();
    layoutWithCurrentConstraints();
    createAlignments();
}

void ACALayout3::removeOverlaps(void)
{
    if (!m_nocsInitialised) initNOCs();
    bool pO = m_preventOverlaps;
    m_preventOverlaps = true;
    layoutWithCurrentConstraints();
    m_preventOverlaps = pO;
}

cola::ConstrainedFDLayout *ACALayout3::getFDLayout(void)
{
    return m_fdlayout;
}

void ACALayout3::addBendPointPenalty(bool b)
{
    m_addBendPointPenalty = b;
}

void ACALayout3::favourLongEdges(bool b)
{
    m_favourLongEdges = b;
}

void ACALayout3::postponeLeaves(bool b)
{
    m_postponeLeaves = b;
}

void ACALayout3::useNonLeafDegree(bool b)
{
    m_useNonLeafDegree = b;
}

void ACALayout3::allAtOnce(bool b)
{
    m_allAtOnce = b;
}

void ACALayout3::aggressiveOrdering(bool b)
{
    m_aggressiveOrdering = b;
}

void ACALayout3::overlapPrevention(ACAOverlapPrevention p)
{
    m_overlapPrevention = p;
}

void ACALayout3::ignoreEdges(std::vector<bool> ignore)
{
    COLA_ASSERT(ignore.size()==(size_t)m_m); // There should be one boolean for each edge.
    m_ignoreEdge = ignore;
}

void ACALayout3::ignoreNodesForOPWithOffsets(std::vector<bool> ignore)
{
    COLA_ASSERT(ignore.size()==(size_t)m_n);
    m_ignoreNodeForOPWithOffsets = ignore;
}

void ACALayout3::setNodeAliases(std::map<int, int> aliases)
{
    m_nodeAliases = aliases;
    // Recompute node degrees and neighbours.
    computeDegrees();
    // Add connections in state table.
    for (int j = 0; j < m_m; j++) {
        cola::Edge e = m_es.at(j);
        int src = e.first, tgt = e.second;
        src = alias(src);
        tgt = alias(tgt);
        (*m_alignmentState)(src,tgt) |= ACACONN;
        (*m_alignmentState)(tgt,src) |= ACACONN;
    }
}

void ACALayout3::setClusterHierarchy(cola::RootCluster *rc)
{
    m_rc = rc;
}

bool ACALayout3::edgeIsAligned(int j)
{
    return m_alignedEdges.count(j)!=0;
}

bool ACALayout3::edgeIsAlignedConstTime(int j)
{
    cola::Edge e = m_es.at(j);
    int src = e.first, tgt = e.second;
    int astate = (*m_alignmentState)(src,tgt);
    return (astate & (ACAHORIZ|ACAVERT));
}

bool ACALayout3::nodesAreAligned(int i, int j)
{
    int astate = (*m_alignmentState)(i,j);
    return (astate & (ACAHORIZ|ACAVERT));
}

void ACALayout3::layoutPeriod(unsigned p)
{
    m_layoutPeriod = p;
}

void ACALayout3::doFinalLayout(bool b)
{
    m_doFinalFDLayout = b;
}

void ACALayout3::addGroupOfNonOverlapExemptRectangles(std::vector<unsigned> rs)
{
    // 1. We maintain a vector of vectors, to be added to ConstrainedFDLayout objects
    //    each time they are created.
    m_nocExemptRects.push_back(rs);
    // 2. We build our own NonOverlapConstraintExemptions object for use in our
    //    initNOCs method, where we set up our NOC objects, which we use in our
    //    feasibility testing on each ACA iteration.
    m_nocExemptions->addExemptGroupOfRectangles(rs);
    // 3. We maintain a multimap mapping each rectangle index to all the rectangle
    //    indices with which it is exempt. This is used when we add a new rectangle
    //    for each aligned edge; namely, when aligning edge e=(u,v) we make this new
    //    rectangle exempt from the union of the exemption sets of u and v. This is
    //    important when we put port dummy nodes /inside/ of the proper nodes to
    //    which they belong, so that the edge rectangles can be exempt from non-overlap
    //    with those proper nodes as well.
    for (std::vector<unsigned>::iterator it=rs.begin(); it!=rs.end(); it++) {
        for (std::vector<unsigned>::iterator jt=rs.begin(); jt!=rs.end(); jt++) {
            if (*it==*jt) continue;
            m_nocExemptionSets.insert(std::pair<unsigned,unsigned>(*it,*jt));
        }
    }
}

std::set<unsigned> ACALayout3::exemptionSetForEdge(int j)
{
    cola::Edge e = m_es[j];
    unsigned src = e.first, tgt = e.second;
    std::set<unsigned> exemptions;
    // src set
    std::pair <std::multimap<unsigned,unsigned>::iterator, std::multimap<unsigned,unsigned>::iterator > srcRange;
    srcRange = m_nocExemptionSets.equal_range(src);
    for (std::multimap<unsigned,unsigned>::iterator it=srcRange.first; it!=srcRange.second; it++) {
        exemptions.insert(it->second);
    }
    // tgt set
    std::pair <std::multimap<unsigned,unsigned>::iterator, std::multimap<unsigned,unsigned>::iterator > tgtRange;
    tgtRange = m_nocExemptionSets.equal_range(tgt);
    for (std::multimap<unsigned,unsigned>::iterator it=tgtRange.first; it!=tgtRange.second; it++) {
        exemptions.insert(it->second);
    }
    return exemptions;
}

void ACALayout3::setAlignmentOffsetsForCompassDirection(ACASepFlag sf, EdgeOffsets offsets)
{
    COLA_ASSERT(offsets.size()==(size_t)m_m); // There should be one offset for each edge.
    m_edgeOffsets.insert( std::pair<ACASepFlag,EdgeOffsets>(sf,offsets) );
}

void ACALayout3::setAllowedDirections(ACASepFlags seps)
{
    COLA_ASSERT(seps.size()==(size_t)m_m); // There should be one flag for each edge.
    m_allowedSeps = seps;
}

void ACALayout3::setAllowedDirections(ACASepFlagsStruct seps)
{
    setAllowedDirections(seps.sepFlags);
}

std::string ACALayout3::writeAlignmentTable(void)
{
    std::string s = m_alignmentState->toString();
    return s;
}

void ACALayout3::computeDegrees(void)
{
    m_nbrs.clear();
    m_leaves.clear();
    m_deg2Nodes.clear();
    m_nlnbrs.clear();
    m_nldeg2Nodes.clear();
    // Map node indices to indices of their neighbours.
    for (int j = 0; j < m_m; j++) {
        cola::Edge e = m_es.at(j);
        m_nbrs.insert(std::pair<int,int>(e.first,e.second));
        m_nbrs.insert(std::pair<int,int>(e.second,e.first));
        m_incidentEdges.insert(std::pair<int,int>(e.first,j));
        m_incidentEdges.insert(std::pair<int,int>(e.second,j));
    }
    // Find the leaves and degree-2 nodes.
    for (int i = 0; i < m_n; i++) {
        int c = m_nbrs.count(i);
        if (c == 1) m_leaves.insert(i);
        if (c == 2) m_deg2Nodes.insert(i);
    }
    // Compute non-leaf neighbours.
    for (int i = 0; i < m_n; i++) {
        // Iterate over the neighbours of node i, retaining only the non-leaves.
        std::pair< std::multimap<int,int>::iterator, std::multimap<int,int>::iterator > range;
        range = m_nbrs.equal_range(i);
        for (std::multimap<int,int>::iterator it=range.first; it!=range.second; ++it) {
            int j = it->second;
            if (m_leaves.count(j)==0) {
                m_nlnbrs.insert(std::pair<int,int>(i,j));
            }
        }
    }
    // Compute non-leaf incident edges.
    for (int j = 0; j < m_m; j++) {
        cola::Edge e = m_es.at(j);
        if (m_leaves.count(e.second)==0) m_nlincidentEdges.insert(std::pair<int,int>(e.first,j));
        if (m_leaves.count(e.first)==0) m_nlincidentEdges.insert(std::pair<int,int>(e.second,j));
    }
    // Find the non-leaf degree-2 nodes.
    for (int i = 0; i < m_n; i++) {
        if (m_nlnbrs.count(i) == 2) m_nldeg2Nodes.insert(i);
    }
}

std::set<int> ACALayout3::getNodesByDegree(int d)
{
    std::multimap<int,int> &nbrs = m_useNonLeafDegree ? m_nlnbrs : m_nbrs;
    std::set<int> nodes;
    for (int i = 0; i < m_n; i++) {
        int c = nbrs.count(i);
        if (c == d) nodes.insert(i);
    }
    return nodes;
}

std::vector<int> ACALayout3::cyclicOrderedEdgesAroundNode(int i)
{
    // Get coords of node i, which will be the root of the edges.
    vpsc::Rectangle *Ri = m_rs.at(i);
    double xi = Ri->getCentreX(), yi = Ri->getCentreY();
    // Get map from node indices to indices of incident edges.
    std::multimap<int,int> &edges = m_useNonLeafDegree ? m_nlincidentEdges : m_incidentEdges;
    // Build vector of rooted edges for node i.
    std::vector<RootedEdge> rootedEdges;
    // Get range of multimap where key is node index i.
    std::pair< std::multimap<int,int>::iterator, std::multimap<int,int>::iterator > range;
    range = edges.equal_range(i);
    // Iterate over this range, to consider each edge incident to the node.
    for (std::multimap<int,int>::iterator it=range.first; it!=range.second; ++it) {
        int j = it->second; // j is index of incident edge
        // Get index k of other endpoint.
        cola::Edge e = m_es.at(j);
        int k = e.first==(unsigned)i ? e.second : e.first;
        // Get coords of node k.
        vpsc::Rectangle *Rk = m_rs.at(k);
        double xk = Rk->getCentreX(), yk = Rk->getCentreY();
        // Build rooted edge.
        RootedEdge E(j,k,xk-xi,yk-yi);
        rootedEdges.push_back(E);
    }
    // Now sort by angle as given by atan2.
    std::sort(rootedEdges.begin(), rootedEdges.end(), sortRootedEdges);
    // Build vector of edge indices and return it.
    std::vector<int> ei;
    for (std::vector<RootedEdge>::iterator it=rootedEdges.begin(); it!=rootedEdges.end(); ++it) {
        ei.push_back(it->j);
    }
    return ei;
}

std::vector<int> ACALayout3::cyclicOrderedNbrsAroundNode(int i)
{
    // Get coords of node i, which sits at the centre.
    vpsc::Rectangle *Ri = m_rs.at(i);
    double xi = Ri->getCentreX(), yi = Ri->getCentreY();
    // Get map from node indices to indices of incident edges.
    std::multimap<int,int> &edges = m_useNonLeafDegree ? m_nlincidentEdges : m_incidentEdges;
    // Build vector of rooted edges for node i.
    std::vector<RootedEdge> rootedEdges;
    // Get range of multimap where key is node index i.
    std::pair< std::multimap<int,int>::iterator, std::multimap<int,int>::iterator > range;
    range = edges.equal_range(i);
    // Iterate over this range, to consider each edge incident to the node.
    for (std::multimap<int,int>::iterator it=range.first; it!=range.second; ++it) {
        int j = it->second; // j is index of incident edge
        // Get index k of other endpoint.
        cola::Edge e = m_es.at(j);
        int k = e.first==(unsigned)i ? e.second : e.first;
        // Get coords of node k.
        vpsc::Rectangle *Rk = m_rs.at(k);
        double xk = Rk->getCentreX(), yk = Rk->getCentreY();
        // Build rooted edge.
        RootedEdge E(j,k,xk-xi,yk-yi);
        rootedEdges.push_back(E);
    }
    // Now sort by angle as given by atan2.
    std::sort(rootedEdges.begin(), rootedEdges.end(), sortRootedEdges);
    // Build vector of distal node indices and return it.
    std::vector<int> ni;
    for (std::vector<RootedEdge>::iterator it=rootedEdges.begin(); it!=rootedEdges.end(); ++it) {
        ni.push_back(it->k);
    }
    return ni;
}

std::vector<RootedEdge> ACALayout3::cyclicOrderedRootedEdgesAroundNode(int i)
{
    // Get coords of node i, which sits at the centre.
    vpsc::Rectangle *Ri = m_rs.at(i);
    double xi = Ri->getCentreX(), yi = Ri->getCentreY();
    // Get map from node indices to indices of incident edges.
    std::multimap<int,int> &edges = m_useNonLeafDegree ? m_nlincidentEdges : m_incidentEdges;
    // Build vector of rooted edges for node i.
    std::vector<RootedEdge> rootedEdges;
    // Get range of multimap where key is node index i.
    std::pair< std::multimap<int,int>::iterator, std::multimap<int,int>::iterator > range;
    range = edges.equal_range(i);
    // Iterate over this range, to consider each edge incident to the node.
    for (std::multimap<int,int>::iterator it=range.first; it!=range.second; ++it) {
        int j = it->second; // j is index of incident edge
        // Get index k of other endpoint.
        cola::Edge e = m_es.at(j);
        int k = e.first==(unsigned)i ? e.second : e.first;
        // Get coords of node k.
        vpsc::Rectangle *Rk = m_rs.at(k);
        double xk = Rk->getCentreX(), yk = Rk->getCentreY();
        // Build rooted edge.
        RootedEdge E(j,k,xk-xi,yk-yi);
        rootedEdges.push_back(E);
    }
    // Now sort by angle as given by atan2, and return.
    std::sort(rootedEdges.begin(), rootedEdges.end(), sortRootedEdges);
    return rootedEdges;
}

std::string ACALayout3::configureDeg4Nodes(void)
{
    std::string output = "";
    // Get list of indices of degree-4 nodes.
    std::set<int> nodes = getNodesByDegree(4);

    // FIXME: Should go through the nodes in a good order.
    // For now we just use any order.

    // We will need to allow aggressive ordering.
    aggressiveOrdering(true);

    for (std::set<int>::iterator it=nodes.begin(); it!=nodes.end(); ++it) {
        int rootNode = *it;
        // Get vector of nodes neighbouring this node, in clockwise cyclic order.
        std::vector<RootedEdge> rootedEdgeCycle = cyclicOrderedRootedEdgesAroundNode(rootNode);
        COLA_ASSERT(rootedEdgeCycle.size()==4);
        // Find the ordered alignment with smallest angular displacement for the four
        // nbrs in the four cardinal directions.
        NodeAlignment na = chooseNearestAlignment(rootNode, rootedEdgeCycle);
        int k0 = na.nbrIndex;
        ACASepFlag sf0 = na.direction;

        // Now we want to align nbr k0 in direction sf0, and then let all the others be
        // determined by the existing (clockwise) cyclic ordering of the neighbours.
        // We create as many of these alignments as are feasible, ALLOWING aggressive ordering.
        // Find index i0 of nbr k0 in the cycle.
        int i0 = -1;
        for (int i = 0; i < 4; i++) {
            if (rootedEdgeCycle.at(i).k==k0) {
                i0 = i;
                break;
            }
        }
        COLA_ASSERT(i0>=0);
        std::vector<ACASepFlag> cw = cwCardinalsFrom(sf0);

        // Prepare vector of NodeAlignments.
        NodeAlignments nas;
        for (int di = 0; di < 4; di++) {
            int i = (i0+di)%4;
            RootedEdge re = rootedEdgeCycle.at(i);
            int k = re.k;
            int j = re.j;
            ACASepFlag sf = cw[di];
            NodeAlignment na(rootNode,k,sf,j);
            nas.push_back(na);
        }
        // Create as many of these alignments as are feasible.
        char buf[100];
        sprintf(buf,"-Node%03d",rootNode);
        std::string debugName = "BiComp-07-ACA-Deg4"+std::string(buf);
        output += createFeasibleNodeAlignments(nas,debugName);
    }
    aggressiveOrdering(false);
    return output;
}

std::string ACALayout3::configureDeg3Nodes(void)
{
    std::string output = "";
    // Get list of indices of degree-3 nodes.
    std::set<int> nodes = getNodesByDegree(3);

    // FIXME: Should go through the nodes in a good order.
    // For now we just use any order.

    // We will need to allow aggressive ordering.
    aggressiveOrdering(true);
    for (std::set<int>::iterator it=nodes.begin(); it!=nodes.end(); ++it) {
        int rootNode = *it;
        // Get vector of nodes neighbouring this node, in clockwise cyclic order.
        std::vector<RootedEdge> rootedEdgeCycle = cyclicOrderedRootedEdgesAroundNode(rootNode);
        COLA_ASSERT(rootedEdgeCycle.size()==3);

        // Choose optimal node configuration.
        NodeAlignments nas;

        // FIXME: Should try different configurations if the optimal one is not possible.

        // Choose least aggressive first alignment.
        NodeAlignment na = chooseNearestAlignment(rootNode, rootedEdgeCycle);
        nas.push_back(na);

        // Determine the possible directions for the other neighbours, preserving
        // their cyclic order.

        // Let k0 be index of first aligned neighbour.
        int k0 = na.nbrIndex;
        // Find its index i0 in the nbrcycle.
        int i0 = -1;
        for (int i = 0; i < 3; i++) {
            if (rootedEdgeCycle.at(i).k==k0) {
                i0 = i; break;
            }
        }
        COLA_ASSERT(i0>=0);
        // Let sf0 be the cardinal of the first alignment.
        ACASepFlag sf0 = na.direction;
        // Get the clockwise vector of cardinals starting from that one.
        std::vector<ACASepFlag> cw = cwCardinalsFrom(sf0);
        // Remaining neighbours:
        std::vector<RootedEdge> lastTwo;
        RootedEdge re1 = rootedEdgeCycle[(i0+1)%3];
        RootedEdge re2 = rootedEdgeCycle[(i0+2)%3];
        int K1 = re1.k;
        int K2 = re2.k;
        int J1 = re1.j;
        int J2 = re2.j;
        // Available cardinals:
        ACASepFlags avail;
        avail.push_back( (ACASepFlag)( cw[1] | cw[2] ) );
        avail.push_back( (ACASepFlag)( cw[2] | cw[3] ) );

        // Choose next alignment.
        na = chooseNearestAlignment(rootNode,lastTwo,avail);
        nas.push_back(na);

        // Now determine whether any options remain for the final alignment, or if
        // there is only one place left for it to go.
        int k1 = na.nbrIndex;
        ACASepFlag sf1 = na.direction;
        if (k1==K1 && sf1==cw[2]) {
            // In this case our second choice k1 was the neighbour K1 immediately
            // following k0 clockwise, and it was assigned its second available
            // cardinal cw[2].
            na = NodeAlignment(rootNode,K2,cw[3],J2);
            nas.push_back(na);
        } else if (k1==K2 && sf1==cw[2]) {
            // In this case our second choice k1 was the second neighbour K2
            // following k0 clockwise, and it was assigned its first available
            // cardinal cw[2].
            na = NodeAlignment(rootNode,K1,cw[1],J1);
            nas.push_back(na);
        } else {
            // In this case a choice still remains to be made.
            std::vector<RootedEdge> lastOne;
            ACASepFlag sf;
            if (k1==K1) {
                lastOne.push_back(re2);
                sf = (ACASepFlag)( cw[2] | cw[3] );
            } else { // k1==K2
                lastOne.push_back(re1);
                sf = (ACASepFlag)( cw[1] | cw[2] );
            }
            na = chooseNearestAlignment(rootNode,lastOne,sf);
            nas.push_back(na);
        }

        // Create as many of these alignments as are feasible.
        char buf[100];
        sprintf(buf,"-Node%03d",rootNode);
        std::string debugName = "BiComp-07-ACA-Deg3"+std::string(buf);
        output += createFeasibleNodeAlignments(nas,debugName);
    }
    aggressiveOrdering(false);
    return output;
}

/**
 * Create as many of the passed node alignments as are feasible.
 */
std::string ACALayout3::createFeasibleNodeAlignments(NodeAlignments nas, string debugName)
{
    std::string output="";
    int iteration = 0;
    char buf[1000];
    for (std::vector<NodeAlignment>::iterator it=nas.begin(); it!=nas.end(); ++it) {
        NodeAlignment na = *it;
        int j = na.assocEdge;
        if (m_ignoreEdge[j]) continue;
        if (edgeIsAlignedConstTime(j)) continue;
        int root = na.rootIndex;
        int nbr = na.nbrIndex;
        ACASepFlag sf = na.direction;
        int l = sf==ACANORTH || sf==ACAWEST ? nbr : root;
        int r = l==nbr ? root : nbr;
        OrderedAlignment *oa = initOrdAlign(l,r,sf,j);
        bool applied = applyIfFeasible(oa);
        if (applied) {
            m_ordAligns.push_back(oa);
            // Add the new separated alignment constraints.
            m_ccs.push_back(oa->separation);
            m_ccs.push_back(oa->alignment);
            // Redo the layout, with the new constraints.
            layoutIfAppropriate();
            // Update state tables.
            updateStateTables(oa);
            // Diagnostic output
            sprintf(buf,"Create alignment %d: %d, %d, %d\n",iteration,l,r,sf);
            output += std::string(buf);
        }
        //Optional SVG output:
        if (debugName.length() > 0) {
            sprintf(buf,"-%03d",iteration);
            outputInstanceToSVG(debugName+std::string(buf));
        }
        iteration++;
    }
    return output;
}

/**
 * Consider aligning each neighbour node in each of the available cardinal directions from
 * the root node, and choose such an alignment in which the angular displacement from the
 * existing positions would be minimal.
 * Return the index of the neighbour and the direction in which to align it.
 */
NodeAlignment ACALayout3::chooseNearestAlignment(int rootIndex, std::vector<int> nbrIndices,
                                                            ACASepFlag availableCardinals)
{
    // Angular displacements range from -pi to +pi so 4 is in absolute value greater than any such,
    // thus effectively infinity.
    double a0 = 4;
    ACASepFlag sf0;
    int k0;
    ACASepFlag sf;
    int sn = 1;
    // Iterate over the cardinals.
    while (sn < 16) {
        sf = (ACASepFlag) sn;
        sn = sn << 1;
        // Skip this cardinal if it is not available.
        if ( ! (sf & availableCardinals) ) continue;
        // Iterate over the nbrs.
        for (std::vector<int>::iterator it=nbrIndices.begin(); it!=nbrIndices.end(); ++it) {
            int k = *it; // k is index of a neighbouring node
            double a = abs(changeInAngle(rootIndex,k,sf));
            if (a < a0) {
                a0 = a;
                k0 = k;
                sf0 = sf;
            }
        }
    }
    return NodeAlignment(rootIndex,k0,sf0);
}

/**
 * In this version, you specify a different set of available cardinals for each neighbour.
 */
NodeAlignment ACALayout3::chooseNearestAlignment(int rootIndex, std::vector<int> nbrIndices, ACASepFlags availableCardinals)
{
    size_t N = nbrIndices.size(), Np = availableCardinals.size();
    COLA_ASSERT(N==Np);
    // Angular displacements range from -pi to +pi so 4 is in absolute value greater than any such,
    // thus effectively infinity.
    double a0 = 4;
    ACASepFlag sf0;
    int k0;
    for (size_t i = 0; i < N; i++) {
        int k = nbrIndices.at(i);
        ACASepFlag sfAvail = availableCardinals.at(i);
        // Iterate over cardinals.
        ACASepFlag sf;
        int sn = 1;
        while (sn < 16) {
            sf = (ACASepFlag) sn;
            sn = sn << 1;
            // Skip this cardinal if it is not available.
            if ( ! (sf & sfAvail) ) continue;
            double a = abs(changeInAngle(rootIndex,k,sf));
            if (a < a0) {
                a0 = a;
                k0 = k;
                sf0 = sf;
            }
        }
    }
    return NodeAlignment(rootIndex,k0,sf0);
}


NodeAlignment ACALayout3::chooseNearestAlignment(int rootIndex, std::vector<RootedEdge> rootedEdges,
                                                            ACASepFlag availableCardinals)
{
    // Angular displacements range from -pi to +pi so 4 is in absolute value greater than any such,
    // thus effectively infinity.
    double a0 = 4;
    ACASepFlag sf0;
    int k0;
    int j0;
    ACASepFlag sf;
    int sn = 1;
    // Iterate over the cardinals.
    while (sn < 16) {
        sf = (ACASepFlag) sn;
        sn = sn << 1;
        // Skip this cardinal if it is not available.
        if ( ! (sf & availableCardinals) ) continue;
        // Iterate over the nbrs.
        for (std::vector<RootedEdge>::iterator it=rootedEdges.begin(); it!=rootedEdges.end(); ++it) {
            int k = (*it).k; // k is index of a neighbouring node
            double a = abs(changeInAngle(rootIndex,k,sf));
            if (a < a0) {
                a0 = a;
                k0 = k;
                j0 = (*it).j;
                sf0 = sf;
            }
        }
    }
    return NodeAlignment(rootIndex,k0,sf0,j0);
}

/**
 * In this version, you specify a different set of available cardinals for each neighbour.
 */
NodeAlignment ACALayout3::chooseNearestAlignment(int rootIndex, std::vector<RootedEdge> rootedEdges, ACASepFlags availableCardinals)
{
    size_t N = rootedEdges.size(), Np = availableCardinals.size();
    COLA_ASSERT(N==Np);
    // Angular displacements range from -pi to +pi so 4 is in absolute value greater than any such,
    // thus effectively infinity.
    double a0 = 4;
    ACASepFlag sf0;
    int k0;
    int j0;
    for (size_t i = 0; i < N; i++) {
        int k = rootedEdges.at(i).k;
        ACASepFlag sfAvail = availableCardinals.at(i);
        // Iterate over cardinals.
        ACASepFlag sf;
        int sn = 1;
        while (sn < 16) {
            sf = (ACASepFlag) sn;
            sn = sn << 1;
            // Skip this cardinal if it is not available.
            if ( ! (sf & sfAvail) ) continue;
            double a = abs(changeInAngle(rootIndex,k,sf));
            if (a < a0) {
                a0 = a;
                k0 = k;
                j0 = rootedEdges.at(i).j;
                sf0 = sf;
            }
        }
    }
    return NodeAlignment(rootIndex,k0,sf0,j0);
}


void ACALayout3::replaceOrderedAlignments(std::vector<OrderedAlignment *> oas)
{
    cola::CompoundConstraints newccs;
    // Keep the compound constraints NOT created by the existing OAs.
    std::set<cola::SeparationConstraint*> oldSep;
    std::set<cola::AlignmentConstraint*> oldAl;
    for (std::vector<OrderedAlignment*>::iterator it=m_ordAligns.begin(); it!=m_ordAligns.end(); it++) {
        OrderedAlignment *oa = *it;
        oldSep.insert(oa->separation);
        oldAl.insert(oa->alignment);
    }
    for (cola::CompoundConstraints::iterator it=m_ccs.begin(); it!=m_ccs.end(); it++) {
        cola::SeparationConstraint *sepco = dynamic_cast<cola::SeparationConstraint*>(*it);
        cola::AlignmentConstraint *alco = dynamic_cast<cola::AlignmentConstraint*>(*it);
        if (alco && oldAl.count(alco)!=0) continue;
        if (sepco && oldSep.count(sepco)!=0) continue;
        newccs.push_back(*it);
    }
    // Complete the passed OAs, save them, and add ccs.
    m_ordAligns.clear();
    for (std::vector<OrderedAlignment*>::iterator it=oas.begin(); it!=oas.end(); it++) {
        OrderedAlignment *oa = *it;
        completeOrdAlign(oa);
        m_ordAligns.push_back(oa);
        newccs.push_back(oa->separation);
        newccs.push_back(oa->alignment);
    }
    // Now clear and refill the m_ccs vector.
    m_ccs.clear();
    for (cola::CompoundConstraints::iterator it=newccs.begin(); it!=newccs.end(); it++) {
        m_ccs.push_back(*it);
    }
}

void ACALayout3::generateVPSCConstraints(void)
{
    // First generate one x-variable and one y-variable for each node.
    // Also create a rectangle vector for each dimension.
    m_xrs.resize(m_n);
    m_yrs.resize(m_n);
    for (int i = 0; i < m_n; i++) {
        m_xvs.push_back(new vpsc::Variable(i,m_rs[i]->getCentreX()));
        m_yvs.push_back(new vpsc::Variable(i,m_rs[i]->getCentreY()));
        m_xrs[i] = m_rs[i];
        m_yrs[i] = m_rs[i];
    }
    // Generate all VPSC constraints and any auxiliary variables.
    vpsc::Constraints xcs, ycs;
    for (unsigned k = 0; k < m_ccs.size(); k++) {
        cola::CompoundConstraint *cc = m_ccs.at(k);
        cc->generateVariables(vpsc::XDIM, m_xvs);
        cc->generateVariables(vpsc::YDIM, m_yvs);
        cc->generateSeparationConstraints(vpsc::XDIM, m_xvs, xcs, m_rs);
        cc->generateSeparationConstraints(vpsc::YDIM, m_yvs, ycs, m_rs);
    }

    // DEBUG
    for (Constraints::iterator it=xcs.begin(); it!=xcs.end(); it++) {
        Constraint *c = *it;
        int z=0;
        int y=z;
    }
    // -----

    // Compute how many auxiliary vars we got.
    m_numExtraXVars = m_xvs.size() - m_n;
    m_numExtraYVars = m_yvs.size() - m_n;
    // In each dimension, the rectangle vector needs NULL placeholders to make
    // it the same length as the vector of variables. This is so that when ACA
    // generates guideline variables for alignments, it can create corresponding
    // rectangles for non-overlap constraints involving aligned edges.
    for (int i = 0; i < m_numExtraXVars; i++) m_xrs.push_back(NULL);
    for (int i = 0; i < m_numExtraYVars; i++) m_yrs.push_back(NULL);
    // Store constraints by dimension and by equality vs. inequality constraints.
    for (unsigned k = 0; k < xcs.size(); k++) {
        vpsc::Constraint *c = xcs.at(k);
        m_xcs.push_back(c);
        if (c->equality) {
            m_xEqCs.push_back(c);
        } else {
            m_xIneqCs.push_back(c);
        }
    }
    for (unsigned k = 0; k < ycs.size(); k++) {
        vpsc::Constraint *c = ycs.at(k);
        m_ycs.push_back(c);
        if (c->equality) {
            m_yEqCs.push_back(c);
        } else {
            m_yIneqCs.push_back(c);
        }
    }
}

void ACALayout3::initNOCs(void)
{
    //NonOverlapConstraintExemptions *exX = new NonOverlapConstraintExemptions();
    //NonOverlapConstraintExemptions *exY = new NonOverlapConstraintExemptions();
    m_xnocs = new cola::NonOverlapConstraints(m_nocExemptions);
    m_ynocs = new cola::NonOverlapConstraints(m_nocExemptions);
    for (int i=0; i<m_n; i++) {
        vpsc::Rectangle *R = m_rs[i];
        double hw = R->width()/2.0, hh = R->height()/2.0;
        m_xnocs->addShape(i,hw,hh);
        m_ynocs->addShape(i,hw,hh);
    }
    m_nocsInitialised = true;
}

int ACALayout3::alias(int i)
{
    std::map<int,int>::iterator it = m_nodeAliases.find(i);
    if (it!=m_nodeAliases.end()) i = it->second;
    return i;
}

/**
 * Retrieve a rectangle, using the nodeAliases (if any).
 */
vpsc::Rectangle *ACALayout3::getRect(int i, bool doAlias)
{
    if (doAlias) i = alias(i);
    vpsc::Rectangle *R = m_rs.at(i);
    return R;
}

/**
 * This is for use only during initialisation of the state tables, when we have
 * to deal with the extra variables generated by the incoming constraints.
 * For that time we arrange the variables like this:
 *
 *     ... rectangles ... | ... extra x-vars ... | ... extra y-vars ...
 *
 * So for example if there were 5 rectangles, 2 extra x-vars, and 3 extra y-vars,
 * then the columns (and rows) in the state table corresponding to the 7 x- and 8 y-vars
 * would be:
 *     x: 0 1 2 3 4 | 5 6
 *     y: 0 1 2 3 4 | 7 8 9
 */
int ACALayout3::adjustVarNumForExtraVars(vpsc::Dim dim, int k)
{
    if (dim==vpsc::YDIM && k >= m_n) k += m_numExtraXVars;
    return k;
}

void ACALayout3::initStateTables(void)
{
    // Start by building table large enough to handle each rectangle, as
    // well as each extra X-var, and each extra Y-var.
    int N = m_n + m_numExtraXVars + m_numExtraYVars;
    m_alignmentState  = new Matrix2d<int>(N,N);
    // Initialise with zeroes.
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            (*m_alignmentState)(i,j) = 0;
        }
    }
    // Note connections in alignment state table.
    for (int j = 0; j < m_m; j++) {
        cola::Edge e = m_es.at(j);
        int src = e.first, tgt = e.second;
        (*m_alignmentState)(src,tgt) = ACACONN;
        (*m_alignmentState)(tgt,src) = ACACONN;
    }
    // Consider equality constraints in the x-dimension.
    for (unsigned k = 0; k < m_xEqCs.size(); k++) {
        vpsc::Constraint *c = m_xEqCs.at(k);
        int l = c->left->id, r = c->right->id;
        double gap = c->gap;
        // Adjust for extra variables.
        l = adjustVarNumForExtraVars(vpsc::XDIM,l);
        r = adjustVarNumForExtraVars(vpsc::XDIM,r);
        if (gap==0) {
            // It is an alignment.
            recordAlignmentWithClosure(l,r,ACAVERT,N);
        }
    }
    // Consider equality constraints in the y-dimension.
    for (unsigned k = 0; k < m_yEqCs.size(); k++) {
        vpsc::Constraint *c = m_yEqCs.at(k);
        int l = c->left->id, r = c->right->id;
        double gap = c->gap;
        // Adjust for extra variables.
        l = adjustVarNumForExtraVars(vpsc::YDIM,l);
        r = adjustVarNumForExtraVars(vpsc::YDIM,r);
        if (gap==0) {
            // It is an alignment.
            recordAlignmentWithClosure(l,r,ACAHORIZ,N);
        }
    }
    // Record snapshot for debugging purposes.
    aStateBeforeChop = m_alignmentState->toString();
    // Now that we have computed the transitive closure of all the passed
    // constraints, we will no longer need the rows and columns for the extra
    // variables, so we chop those off now.
    Matrix2d<int> *aState = new Matrix2d<int>(m_n,m_n);
    for (int i = 0; i < m_n; i++) {
        for (int j = 0; j < m_n; j++) {
            (*aState)(i,j) = (*m_alignmentState)(i,j);
        }
    }
    delete m_alignmentState;
    m_alignmentState  = aState;
}

void ACALayout3::recordAlignmentWithClosure(int i, int j, ACAFlag af, int numCols)
{
    if (numCols == 0) numCols = m_n;
    // Get the set Ai of all indices already aligned with i, including i itself.
    // Do likewise for j.
    std::set<int> Ai, Aj;
    Ai.insert(i);
    Aj.insert(j);
    for (int k = 0; k < numCols; k++) {
        if ( (*m_alignmentState)(i,k) & af ) Ai.insert(k);
        if ( (*m_alignmentState)(j,k) & af ) Aj.insert(k);
    }
    // Record that everything in Ai is aligned with everything in Aj.
    // This is the transitive closure of the new alignment for the alignments table.
    for (std::set<int>::iterator it=Ai.begin(); it!=Ai.end(); ++it) {
        for (std::set<int>::iterator jt=Aj.begin(); jt!=Aj.end(); ++jt) {
            (*m_alignmentState)(*it,*jt) |= af;
            (*m_alignmentState)(*jt,*it) |= af;
        }
    }
}

void ACALayout3::addOrderedAlignments(OrderedAlignments oas)
{
    for (OrderedAlignments::const_iterator it=oas.begin(); it!=oas.end(); ++it) {
        OrderedAlignment *oa = *it;
        m_ordAligns.push_back(oa);
        m_ccs.push_back(oa->separation);
        m_ccs.push_back(oa->alignment);
        updateStateTables(oa);
    }
}

void ACALayout3::updateStateTables(OrderedAlignment *oa)
{
    int l = oa->left, r = oa->right;
    recordAlignmentWithClosure(l,r,oa->af);
}

cola::UnsatisfiableConstraintInfos *ACALayout3::unsatX(void)
{
    return m_unsatX;
}

cola::UnsatisfiableConstraintInfos *ACALayout3::unsatY(void)
{
    return m_unsatY;
}

cola::CompoundConstraints ACALayout3::writeAllVPSCConstraintsAsCompound(void)
{
    pushState();
    updateNodeRectsFromVars();
    recomputeEdgeShapes(XDIM);
    recomputeEdgeShapes(YDIM);
    m_xnocs->generateSeparationConstraints(XDIM,m_xvs,m_xcs,m_xrs);
    m_ynocs->generateSeparationConstraints(YDIM,m_yvs,m_ycs,m_yrs);
    cola::CompoundConstraints ccs;
    for (vpsc::Constraints::iterator it=m_xcs.begin(); it!=m_xcs.end(); it++) {
        Constraint *c = *it;
        int l = c->left->id, r = c->right->id;
        if (l>=m_n) l = m_xAuxRectIndexInExtendedRS.at(l) + m_n;
        if (r>=m_n) r = m_xAuxRectIndexInExtendedRS.at(r) + m_n;
        ccs.push_back(new cola::SeparationConstraint(
            XDIM, l, r, c->gap, c->equality
        ));
    }
    for (vpsc::Constraints::iterator it=m_ycs.begin(); it!=m_ycs.end(); it++) {
        Constraint *c = *it;
        int l = c->left->id, r = c->right->id;
        if (l>=m_n) l = m_yAuxRectIndexInExtendedRS.at(l) + m_n;
        if (r>=m_n) r = m_yAuxRectIndexInExtendedRS.at(r) + m_n;
        ccs.push_back(new cola::SeparationConstraint(
            YDIM, l, r, c->gap, c->equality
        ));
    }
    popState();
    return ccs;
}

vpsc::Rectangles ACALayout3::properAndAuxRects(void)
{
    using namespace vpsc;
    Rectangles rs;
    for (Rectangles::iterator it=m_rs.begin(); it!=m_rs.end(); it++) rs.push_back(*it);
    for (Rectangles::iterator it=m_extendedRS.begin(); it!=m_extendedRS.end(); it++) rs.push_back(*it);
    return rs;
}

void ACALayout3::layoutWithCurrentConstraints(void)
{
    bool useNewMethod = false;
    delete m_fdlayout;
    bool preventOverlaps = false;
    vpsc::Rectangles rs = useNewMethod ? properAndAuxRects() : m_rs;
    m_fdlayout = new cola::ConstrainedFDLayout(rs,m_es,m_idealLength,
                                               preventOverlaps,m_edgeLengths,
                                               m_doneTest,m_preIteration);

    m_unsatX = new UnsatisfiableConstraintInfos();
    m_unsatY = new UnsatisfiableConstraintInfos();
    m_fdlayout->setUnsatisfiableConstraintInfo(m_unsatX,m_unsatY);

    cola::CompoundConstraints ccs = useNewMethod ? writeAllVPSCConstraintsAsCompound() : m_ccs;
    m_fdlayout->setConstraints(ccs);
    m_fdlayout->setClusterHierarchy(m_rc);

    if (!useNewMethod) {
        for (std::vector< std::vector<unsigned> >::iterator it=m_nocExemptRects.begin();
             it != m_nocExemptRects.end(); it++) {
            m_fdlayout->addGroupOfNonOverlapExemptRectangles(*it);
        }
    }

    //m_fdlayout->m_doYAxisFirst = true;
    m_fdlayout->run();
}

// Slow and hacky:
void ACALayout3::outputInstanceToSVG(string filename)
{
    cola::ConstrainedFDLayout fdlayout(m_rs,m_es,m_idealLength,
                                       m_preventOverlaps,m_edgeLengths,
                                       m_doneTest,m_preIteration);
    fdlayout.outputInstanceToSVG(filename);
}

// Only do a layout if some condition is met, e.g. stress has accumulated enough,
// or period has been completed.
// Returned boolean says whether we did a layout.
bool ACALayout3::layoutIfAppropriate(void)
{
    m_didLayoutForLastAlignment = false;
    // TODO: Try something more sophisticated; maybe make stress measurement
    // an option.
    unsigned N = m_ordAligns.size();
    if (N>0 && N%m_layoutPeriod==0) {
        layoutWithCurrentConstraints();
        m_didLayoutForLastAlignment = true;
    }
    return m_didLayoutForLastAlignment;
}

bool ACALayout3::acaLoopOnce(void)
{
    OrderedAlignment *oa = chooseOA2();
    if (oa) {
        // Add the new separated alignment constraints.
        m_ccs.push_back(oa->separation);
        m_ccs.push_back(oa->alignment);
        // Redo the layout, with the new constraints.
        layoutIfAppropriate();
        // Update state tables.
        updateStateTables(oa);
        return true;
    } else {
        // We'll come to this case when there are no more alignments to be made.
        // Do a layout if one wasn't done for the last alignment.
        if (m_doFinalFDLayout && !m_didLayoutForLastAlignment) layoutWithCurrentConstraints();
        return false;
    }
}

void ACALayout3::acaLoopOneByOne(void)
{
    // Choose a first alignment.
    OrderedAlignment *oa = chooseOA2();
    while (oa) {
        // Add the new separated alignment constraints.
        m_ccs.push_back(oa->separation);
        m_ccs.push_back(oa->alignment);
        // Redo the layout, with the new constraints.
        layoutIfAppropriate();
        // Update state tables.
        updateStateTables(oa);
        // Choose next ordered alignment.
        oa = chooseOA2();
    }
    // If no layout was done on the final run, do one now.
    if (m_doFinalFDLayout && !m_didLayoutForLastAlignment) {
        layoutWithCurrentConstraints();
    }
}

void ACALayout3::acaLoopAllAtOnce(void)
{
    // Choose a first alignment.
    OrderedAlignment *oa = chooseOA2();
    while (oa) {
        // Add the new separated alignment constraints.
        m_ccs.push_back(oa->separation);
        m_ccs.push_back(oa->alignment);
        // Update state tables.
        updateStateTables(oa);
        // Choose next ordered alignment.
        oa = chooseOA2();
    }
    // Redo the layout, with the new constraints.
    layoutWithCurrentConstraints();
}

bool ACALayout3::allOrNothing(OrderedAlignments oas)
{
    bool okay = true;
    pushState();
    pushRectCoords();
    for (OrderedAlignments::const_iterator it=oas.begin(); it!=oas.end(); ++it) {
        OrderedAlignment *oa = *it;
        okay = applyIfFeasible(oa);
        if (!okay) break;
    }
    if (!okay) {
        //perror("All or nothing: nothing.");
        popRectCoords();
        // Must trash any remaining edge shapes before popping the state.
        trashNewEdgeShapesAccordingToStateStack();
        popState();
    } else {
        //perror("All or nothing: all.");
        dropRectCoords();
        dropState();
        addOrderedAlignments(oas);
        layoutIfAppropriate();
    }
    //layoutWithCurrentConstraints();
    return okay;
}

void ACALayout3::updateVarsAndCons(OrderedAlignment *oa)
{
    oa->separation->generateVariables(vpsc::XDIM, m_xvs);
    oa->separation->generateVariables(vpsc::YDIM, m_yvs);
    oa->separation->generateSeparationConstraints(vpsc::XDIM, m_xvs, m_xcs, m_rs);
    oa->separation->generateSeparationConstraints(vpsc::YDIM, m_yvs, m_ycs, m_rs);
    oa->alignment->generateVariables(vpsc::XDIM, m_xvs);
    oa->alignment->generateVariables(vpsc::YDIM, m_yvs);
    if (oa->edgeIndex>=0) {
        // Associate edge with guideline variable generated for the alignment.
        int g = oa->af==ACAHORIZ ? m_yvs.size() : m_xvs.size();
        m_edgeIndexToGuidelineIndex.insert(std::pair<int,int>(oa->edgeIndex,g));
    }
    oa->alignment->generateSeparationConstraints(vpsc::XDIM, m_xvs, m_xcs, m_rs);
    oa->alignment->generateSeparationConstraints(vpsc::YDIM, m_yvs, m_ycs, m_rs);
}

// Record lengths of variable and constraint vectors, so we can restore them later.
void ACALayout3::pushState(void)
{
    unsigned nxv=m_xvs.size(), nyv=m_yvs.size(),
             nxc=m_xcs.size(), nyc=m_ycs.size(),
             nxr=m_xrs.size(), nyr=m_yrs.size();
    m_sizeStack.push_back(nxv);
    m_sizeStack.push_back(nyv);
    m_sizeStack.push_back(nxc);
    m_sizeStack.push_back(nyc);
    m_sizeStack.push_back(nxr);
    m_sizeStack.push_back(nyr);
}

void ACALayout3::trashNewEdgeShapesAccordingToStateStack(void)
{
    // Check how many variables there were in both dimensions,
    // in the previous state.
    size_t N = m_sizeStack.size();
    COLA_ASSERT(N>=6);
    unsigned nyv=m_sizeStack[N-5];
    unsigned nxv=m_sizeStack[N-6];
    // And how many are there now?
    unsigned Nyv = m_yvs.size();
    unsigned Nxv = m_xvs.size();
    // Revert the data structures that grow with addition
    // of edge shapes.
    // x-dimension
    for (unsigned i = Nxv - 1; i >= nxv; i--) {
        m_xGuidelineIndexToEdgeIndex.erase(i);
        m_xnocs->removeShape(i);
        m_xAuxRectIndexInExtendedRS.erase(i);
    }
    // y-dimension
    for (unsigned j = Nyv - 1; j >= nyv; j--) {
        m_yGuidelineIndexToEdgeIndex.erase(j);
        m_ynocs->removeShape(j);
        m_yAuxRectIndexInExtendedRS.erase(j);
    }
    // As for the m_extendedRS vector, there should be one new
    // rectangle at the end of it for each of the edge shapes.
    // We pop these off the vector now. (Could have done so in
    // in the two loops above, but that would put cleverness over
    // clarity.)
    for (unsigned k = 0; k < (Nxv-nxv)+(Nyv-nyv); k++) {
        m_extendedRS.pop_back();
    }
}

// Restore lengths of variable and constraint vectors, by erasing tails.
void ACALayout3::popState(void)
{
    COLA_ASSERT(m_sizeStack.size()>=6);
    unsigned nyr=m_sizeStack.back(); m_sizeStack.pop_back();
    unsigned nxr=m_sizeStack.back(); m_sizeStack.pop_back();
    unsigned nyc=m_sizeStack.back(); m_sizeStack.pop_back();
    unsigned nxc=m_sizeStack.back(); m_sizeStack.pop_back();
    unsigned nyv=m_sizeStack.back(); m_sizeStack.pop_back();
    unsigned nxv=m_sizeStack.back(); m_sizeStack.pop_back();
    m_xvs.erase(m_xvs.begin()+nxv,m_xvs.end());
    m_yvs.erase(m_yvs.begin()+nyv,m_yvs.end());
    m_xcs.erase(m_xcs.begin()+nxc,m_xcs.end());
    m_ycs.erase(m_ycs.begin()+nyc,m_ycs.end());
    m_xrs.erase(m_xrs.begin()+nxr,m_xrs.end());
    m_yrs.erase(m_yrs.begin()+nyr,m_yrs.end());
}

// Simply pop the last 6 sizes off the size stack.
void ACALayout3::dropState(void)
{
    COLA_ASSERT(m_sizeStack.size()>=6);
    for (int i = 0; i < 6; i++) m_sizeStack.pop_back();
}

void ACALayout3::pushRectCoords(void)
{
    for (int i = 0; i < m_n; i++) {
        Rectangle *R = m_rs[i];
        m_rectXStack.push_back(R->getCentreX());
        m_rectYStack.push_back(R->getCentreY());
    }
}

void ACALayout3::popRectCoords(void)
{
    COLA_ASSERT(m_rectXStack.size()>=m_n);
    COLA_ASSERT(m_rectYStack.size()>=m_n);
    for (int i = 0; i < m_n; i++) {
        double x = m_rectXStack.back(); m_rectXStack.pop_back();
        double y = m_rectYStack.back(); m_rectYStack.pop_back();
        Rectangle *R = m_rs[m_n-1-i];
        R->moveCentreX(x);
        R->moveCentreY(y);
    }
}

void ACALayout3::dropRectCoords(void)
{
    COLA_ASSERT(m_rectXStack.size()>=m_n);
    COLA_ASSERT(m_rectYStack.size()>=m_n);
    for (int i = 0; i < m_n; i++) {
        m_rectXStack.pop_back();
        m_rectYStack.pop_back();
    }
}

// gi:  index of a guideline
// dim: vpsc::HORIZONTAL if this is a horizontal alignment guideline, VERTICAL if vertical
std::pair<double,double> ACALayout3::getEdgeShapeDims(int gi, vpsc::Dim dim)
{
    int ei = m_guidelineIndexToEdgeIndex[gi];
    cola::Edge e = m_es[ei];
    vpsc::Rectangle *srcR = m_rs[e.first], *tgtR = m_rs[e.second];
    double srcZ = srcR->getCentreD(dim), tgtZ = tgtR->getCentreD(dim);
    vpsc::Rectangle *lowR  = srcZ < tgtZ ? srcR : tgtR;
    vpsc::Rectangle *highR = srcZ < tgtZ ? tgtR : srcR;
    double W=0, H=0;
    if (dim==ACAHORIZ) {
        H = 2*EDGE_SHAPE_HALF_THICKNESS;
        W = highR->getMinX() - lowR->getMaxX() - 2*EDGE_SHAPE_BUFFER;
    } else {
        W = 2*EDGE_SHAPE_HALF_THICKNESS;
        H = highR->getMinY() - lowR->getMaxY() - 2*EDGE_SHAPE_BUFFER;
    }
    return std::pair<double,double>(W,H);
}

vpsc::Rectangle *ACALayout3::makeRectForOA(OrderedAlignment *oa)
{
    return makeRectForEdge(oa->edgeIndex,oa->dim);
}

vpsc::Rectangle *ACALayout3::makeRectForEdge(int j, vpsc::Dim dim)
{
    vpsc::Rectangle *R = new vpsc::Rectangle(0,1,0,1);
    updateRectForEdge(R,j,dim);
    return R;
}

void ACALayout3::updateRectForEdge(Rectangle *R, int j, Dim dim)
{
    // Here dim should be HORIZONTAL when edge j has been horizontally
    // aligned and R is supposed to be a wide box with a narrow height.
    // Vice versa for VERTICAL.
    cola::Edge e = m_es[j];
    vpsc::Rectangle *srcR = m_rs[e.first], *tgtR = m_rs[e.second];
    double srcZ = srcR->getCentreD(dim), tgtZ = tgtR->getCentreD(dim);
    vpsc::Rectangle *lowR  = srcZ < tgtZ ? srcR : tgtR;
    vpsc::Rectangle *highR = srcZ < tgtZ ? tgtR : srcR;
    double x=0,X=0,y=0,Y=0;
    if (dim==HORIZONTAL) {
        X = highR->getMinX() - EDGE_SHAPE_BUFFER;
        x = lowR->getMaxX()  + EDGE_SHAPE_BUFFER;
        double avgY = ( lowR->getCentreY() + highR->getCentreY() ) / 2;
        y = avgY - EDGE_SHAPE_HALF_THICKNESS;
        Y = avgY + EDGE_SHAPE_HALF_THICKNESS;
    } else {
        Y = highR->getMinY() - EDGE_SHAPE_BUFFER;
        y = lowR->getMaxY()  + EDGE_SHAPE_BUFFER;
        double avgX = ( lowR->getCentreX() + highR->getCentreX() ) / 2;
        x = avgX - EDGE_SHAPE_HALF_THICKNESS;
        X = avgX + EDGE_SHAPE_HALF_THICKNESS;
    }
    R->setMinD(XDIM,x);
    R->setMaxD(XDIM,X);
    R->setMinD(YDIM,y);
    R->setMaxD(YDIM,Y);
}

void ACALayout3::recomputeEdgeShapes(Dim dim)
{
    // There is some risk of confusion here.
    // When the dimension passed is XDIM, we will take this to mean that
    // we are interested in computing non-overlap constraints in the
    // x vars. This means that it is the /vertical/ edge shapes that need
    // to be updated, i.e. the shapes representing vertically aligned
    // edges, for which overlap removal means pushing nodes off to the
    // left or right as needed.
    // Conversely, if the dimension passed is YDIM, then we want to
    // update the horizontal edge shapes and prepare for generation of
    // non-overlap constraints on the y variables.
    bool horiz = dim == YDIM;
    int e = horiz ? m_numExtraYVars : m_numExtraXVars;
    int b = m_n + e;
    vpsc::Variables &vs = horiz ? m_yvs : m_xvs;
    int N = vs.size();
    std::map<int,int> &gi2ei = horiz ? m_yGuidelineIndexToEdgeIndex :
                                       m_xGuidelineIndexToEdgeIndex;
    vpsc::Rectangles &rs = horiz ? m_yrs : m_xrs;
    cola::NonOverlapConstraints *nocs = horiz ? m_ynocs : m_xnocs;
    for (int gi = b; gi < N; gi++) {
        int ei = gi2ei[gi];
        Rectangle *R = rs[gi];
        Dim perpDim = dim == YDIM ? XDIM : YDIM;
        updateRectForEdge(R,ei,perpDim);
        nocs->resizeShape(gi,R->width()/2.0,R->height()/2.0);
    }
}

void ACALayout3::updateNodeRectsFromVars(void)
{
    for (int i=0; i<m_n; i++) {
        m_rs[i]->moveCentreX(m_xvs[i]->finalPosition);
        m_rs[i]->moveCentreY(m_yvs[i]->finalPosition);
    }
}

void ACALayout3::updateVarsFromNodeRects(void)
{
    for (int i=0; i<m_n; i++) {
        Rectangle *r = m_rs[i];
        double x=r->getCentreX(), y=r->getCentreY();
        m_xvs[i]->desiredPosition=x;
        m_yvs[i]->desiredPosition=y;
    }
}

void ACALayout3::generateNonoverlapVarsAndCons(Dim dim)
{
    // REDO
    /*
    // If dim==YDIM, then:
    //   Between each horizontally-aligned edge and each node in the x-interval covered by it,
    //      generate a non-overlap constraint in the y-dimension
    // If dom==XDIM, then:
    //   Between each vertically-aligned edge and each node in the y-interval covered by it,
    //      generate a non-overlap constraint in the x-dimension
    cola::NonOverlapConstraints *noc = new cola::NonOverlapConstraints();
    vpsc::Variables &vs = dim==XDIM ? m_xvs : m_yvs;
    vpsc::Constraints &cs = dim==XDIM ? m_xcs : m_ycs;
    // Add shapes for aligned edges.
    for (std::vector<OrderedAlignment*>::iterator it=m_ordAligns.begin(); it!= m_ordAligns.end(); it++) {
        OrderedAlignment *oa = *it;
        if (oa->edgeIndex<0) continue;
        if ((oa->af==ACAHORIZ&&dim==XDIM) || (oa->af==ACAVERT&&dim==YDIM)) continue;
        std::pair<double,double> dims = getEdgeShapeDims(oa);
        int id = m_edgeIndexToGuidelineIndex.at(oa->edgeIndex);
        noc->addShape(id,dims.first/2,dims.second/2);
    }
    // Add nodes.
    for (int i = 0; i < m_n; i++) {
        vpsc::Rectangle *R = m_rs.at(i);
        int id = vs.at(i)->id;
        noc->addShape(id,R->width()/2.0,R->height()/2.0);
    }
    noc->generateVariables(dim,vs);
    noc->generateSeparationConstraints(dim,vs,cs,m_rs);
    */
}

OrderedAlignment *ACALayout3::chooseOA2(void)
{
    // First sort potential alignments by penalty score.
    std::vector<OrderedAlignment*> oas;
    for (int j = 0; j < m_m; j++) {
        if (m_ignoreEdge[j]) continue;
        if (edgeIsAlignedConstTime(j)) continue;
        // Otherwise consider each direction.
        ACASepFlag sf;
        int sn = 1;
        while (sn < 16) {
            sf = (ACASepFlag) sn;
            sn = sn << 1;
            OrderedAlignment *oa = initOrdAlign(j,sf);
            oa->penalty = computePenalty(j,sf);
            oas.push_back(oa);
        }
    }
    std::sort(oas.begin(),oas.end(),sortOrdAlignsByPenalty);
    // Now, starting with the lowest-penalty alignments, look for a feasible one.
    OrderedAlignment *nextOA = NULL;
    for (std::vector<OrderedAlignment*>::iterator it=oas.begin(); it!=oas.end(); it++) {
        OrderedAlignment *oa = *it;
        bool applied = applyIfFeasible(oa);
        if (applied) {
            nextOA = oa; //needn't call completeOrdAlign, since already done by applyIfFeasible
            m_ordAligns.push_back(oa);
            break;
        }
    }
    // TODO: Fix memory leak: delete all OAs that are not being returned.
    return nextOA;
}

OrderedAlignment *ACALayout3::chooseOA(void)
{
    OrderedAlignment *oa = NULL;
    // Initialise minPenalty to a value exceeding the maximum penalty
    // any edge can actually be assigned (so effectively infinity).
    double minPenalty = PENALTY_BOUND;
    // Before considering edges for potential alignment, add the nonoverlap constraints.
    pushState();
    generateNonoverlapVarsAndCons(XDIM);
    generateNonoverlapVarsAndCons(YDIM);
    // Consider each edge for potential alignment.
    for (int j = 0; j < m_m; j++) {
        if (m_ignoreEdge[j]) continue;
        cola::Edge e = m_es.at(j);
        int src = e.first, tgt = e.second;
        // Do aliasing
        src = alias(src);
        tgt = alias(tgt);
        // If already aligned, then skip this edge.
        int astate = (*m_alignmentState)(src,tgt);
        if (astate & (ACAHORIZ|ACAVERT)) continue;
        // Otherwise consider placing tgt in each of the compass directions from src.
        ACASepFlag sf;
        int sn = 1;
        while (sn < 16) {
            sf = (ACASepFlag) sn;
            // shift left to next cardinal direction (do it now in case of 'continue's below)
            sn = sn << 1;
            // Check feasibility.
            if (!isFeasible(j,sf)) continue;
            // Passed feasibility tests.
            // Now compute penalty.
            double p = 0;
            // Basic penalty:
            if (m_favourLongEdges) {
                // Length:
                p += lengthPenaltyForEdge(j);
            } else {
                // Deflection:
                p += deflectionForEdge(j,sf);
            }
            // Bend points:
            if (m_addBendPointPenalty) p += bendPointPenalty(src,tgt,sf);
            // Leaves:
            if (m_postponeLeaves) p += leafPenalty(src,tgt);
            // Replace current ordered alignment if penalty is lower.
            if (p < minPenalty) {
                minPenalty = p;
                delete oa;
                oa = initOrdAlign(j,sf);
            }
        }
    }
    // Restore state.
    popState();
    // Did we find an alignment?
    if (oa) {
        // If so, then complete the OrderedAlignment object, and record it.
        completeOrdAlign(oa);
        m_ordAligns.push_back(oa);
        m_alignedEdges.insert(oa->edgeIndex);
    }
    return oa;
}

OrderedAlignment *ACALayout3::initOrdAlign(int j, ACASepFlag sf)
{
    OrderedAlignment *oa = new OrderedAlignment();
    cola::Edge e = m_es.at(j);
    int src = e.first, tgt = e.second;
    //src = alias(src);
    //tgt = alias(tgt);
    oa->af = sepToAlignFlag(sf);
    oa->sf = sf;
    oa->dim = oa->af == ACAHORIZ ? HORIZONTAL : VERTICAL;
    //oa->left  = sf==ACANORTH || sf==ACAWEST ? tgt : src;
    //oa->right = oa->left == tgt ? src : tgt;
    oa->left = src;
    oa->right = tgt;
    /*
    EdgeOffset offset = getEdgeOffsetForCompassDirection(j,sf);
    oa->offsetLeft  = oa->left == src ? offset.first : offset.second;
    oa->offsetRight = oa->left == src ? offset.second : offset.first;
    */
    oa->edgeIndex = j;
    return oa;
}

OrderedAlignment *ACALayout3::initOrdAlign(int l, int r, ACASepFlag sf, int edgeIndex)
{
    OrderedAlignment *oa = new OrderedAlignment();
    oa->sf = sf;
    oa->af = sepToAlignFlag(sf);
    oa->dim = oa->af == ACAHORIZ ? HORIZONTAL : VERTICAL;
    oa->left  = l;
    oa->right = r;
    oa->edgeIndex = edgeIndex;
    return oa;
}

void ACALayout3::completeOrdAlign(OrderedAlignment *oa)
{
    // FIXME: The fields in OrderedAlignment class really should not be
    // called "left" and "right", since we are actually thinking of them
    // as a "source" and "target". Note that this last notion is itself
    // independent of whether the edge is thought of as directed, and, if
    // so, which are the source and target nodes w.r.t. that directedness;
    // instead, this notion of "source" and "target" refers only to the
    // ACASEPFLAG stored in the OrderedAlignment's sf field.
    int pseudoSource = oa->left, pseudoTarget = oa->right;
    // However, in order to create a SeparationConstraint, we do need to
    // work out which one is actually to be on the "left" and which on the
    // "right" -- this in the VPSC sense, which is literal when we work in
    // the x-dimension, and where "left" means "above" and "right" means
    // "below" when we work in the y-dimension.
    int l = -1, r = -1;
    if (oa->af == ACAHORIZ) {
        if (oa->sf & ACAEAST) {
            l = pseudoSource;
            r = pseudoTarget;
        } else {
            l = pseudoTarget;
            r = pseudoSource;
        }
    } else {
        if (oa->sf & ACASOUTH) {
            l = pseudoSource;
            r = pseudoTarget;
        } else {
            l = pseudoTarget;
            r = pseudoSource;
        }
    }
    COLA_ASSERT(l >= 0);
    COLA_ASSERT(r >= 0);
    //int l = oa->left, r = oa->right;
    vpsc::Rectangle *rl = getRect(l), *rr = getRect(r);
    // Determine dimensions.
    vpsc::Dim sepDim   = oa->af == ACAHORIZ ? vpsc::XDIM : vpsc::YDIM;
    vpsc::Dim alignDim = oa->af == ACAHORIZ ? vpsc::YDIM : vpsc::XDIM;
    // Create the separation constraint.
    double sep = oa->af == ACAHORIZ ?
                (rl->width()+rr->width())/2.0 : (rl->height()+rr->height())/2.0;
    oa->separation = new cola::SeparationConstraint(sepDim,l,r,sep);
    // Create the alignment constraint.
    double pos = oa->af==ACAHORIZ ?
                (rl->getCentreY()+rr->getCentreY())/2.0 : (rl->getCentreX()+rr->getCentreX())/2.0;
    oa->alignment = new cola::AlignmentConstraint(alignDim,pos);
    oa->alignment->addShape(l,oa->offsetLeft);
    oa->alignment->addShape(r,oa->offsetRight);
}

std::vector<OrderedAlignment*> ACALayout3::orderedAlignments(void)
{
    return m_ordAligns;
}

OrderedAlignment *ACALayout3::mostRecentOA(void)
{
    OrderedAlignment *oa = NULL;
    if (!m_ordAligns.empty()) {
        oa = m_ordAligns.back();
    }
    return oa;
}

/// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
/// Feasibility Checking

/**
 * Check using VPSC whether the proposed OA is feasible, and, if so, apply it.
 * Return value says whether or not OA was feasible (and hence applied).
 */
#define FULLSOLVE
bool ACALayout3::applyIfFeasible(OrderedAlignment *oa)
{
    /*
    // DEBUG ---------
    int src = oa->left, tgt = oa->right;
    if ((src == 40 && tgt == 38) || (src == 38 && tgt == 40)) {
        perror("foo");
    }
    // ---------------
    */

    // First check whether it is simply a bad separation.
    //int edgeIndex = oa->edgeIndex;
    //if (edgeIndex >= 0 && badSeparation(edgeIndex,oa->sf)) return false;
    // FIXME:
    // We no longer check m_allowedSeps, since those are specified per edge,
    // and we have found that the edge-based basSep check is no good, since
    // it confuses the src and tgt of the edge with the pseudo src and tgt
    // of the OA.
    if (badSeparation(oa->left, oa->right, oa->sf)) return false;

    /*
    // DEBUG --------
    printf("\n============================================\n");
    printf("OA from %d to %d\n", oa->left, oa->right);
    // --------------
    */

    // If not, then proceed to use VPSC to check feasibility.
    using namespace vpsc;
    // Save the state before we make any changes.
    pushState();
    // Get dimensions.
    bool horiz = oa->dim==HORIZONTAL;
    Dim sepd = horiz ? XDIM : YDIM;
    Dim alnd = horiz ? YDIM : XDIM;
    // Assign vectors based on dimension.
    Variables   &sepv = horiz ? m_xvs : m_yvs;
    Constraints &sepc = horiz ? m_xcs : m_ycs;
    Rectangles  &sepr = horiz ? m_xrs : m_yrs;
    Variables   &alnv = horiz ? m_yvs : m_xvs;
    Constraints &alnc = horiz ? m_ycs : m_xcs;
    Rectangles  &alnr = horiz ? m_yrs : m_xrs;

    /*
    // DEBUG --------------------
    for (Constraints::const_iterator it=alnc.begin(); it!=alnc.end(); ++it) {
        Constraint *c = *it;
        printf("ac: %d, %d\n", c->left->id, c->right->id);
    }
    // --------------------------
    */

    // NOC objects
    cola::NonOverlapConstraints *sepnocs = horiz ? m_xnocs : m_ynocs;
    cola::NonOverlapConstraints *alnnocs = horiz ? m_ynocs : m_xnocs;
    // TODO: Instead of all the maps (gi2ei, auxRecToExtRS), maybe should use more
    // object-oriented solution here. Namely, can't we have a class that contains
    // a guideline, an edge, and a rectangle?
    // Get map from guideline indices to edge indices for alignment dimension.
    std::map<int,int> &gi2ei = horiz ? m_yGuidelineIndexToEdgeIndex :
                                       m_xGuidelineIndexToEdgeIndex;
    // Get map from auxiliary variable indices to indices in 'extendedRS' vector, which
    // contains pointers to all auxiliary rectangles.
    std::map<int,int> &auxRectToExtRS = horiz ? m_yAuxRectIndexInExtendedRS :
                                                m_xAuxRectIndexInExtendedRS;
    // Complete the OA, building its Sep and Align compound constraints.
    completeOrdAlign(oa);
    // Extend vectors.
    oa->separation->generateSeparationConstraints(sepd,sepv,sepc,sepr);
    oa->alignment->generateVariables(alnd,alnv);

    /*
    // DEBUG ----
    int NAC = alnc.size();
    // ----------
    */

    oa->alignment->generateSeparationConstraints(alnd,alnv,alnc,alnr);

    /*
    // DEBUG -----
    NAC = alnc.size() - NAC;
    for (Constraints::const_iterator it=alnc.begin(); it!=alnc.end(); ++it) {
        Constraint *c = *it;
        printf("ac: %d, %d, %.2f, %d\n", c->left->id, c->right->id,
               c->gap, c->equality);
    }
    printf("%d new ones\n", NAC);
    // -----------
    */

    // New rectangle for aligned edge:
    Rectangle *newRect = makeRectForOA(oa);
    alnr.push_back(newRect);
    int newRectIndex = alnv.size()-1;
    gi2ei.insert(std::pair<int,int>(newRectIndex,oa->edgeIndex));
    alnnocs->addShapeWithExemptions(newRectIndex, newRect->width()/2.0, newRect->height()/2.0,
                                    exemptionSetForEdge(oa->edgeIndex));
    m_extendedRS.push_back(newRect);
    auxRectToExtRS.insert(std::pair<int,int>(newRectIndex,m_extendedRS.size()-1));
    // Save this state. We will return to this state if the OrderedAlignment
    // in question turns out to be feasible, and we want to keep it.
    pushState();
    // Save current coordinates of nodes.
    pushRectCoords();
    // Check for satisfiability of the separation and alignment constraints,
    // and of non-overlap constraints in both dimensions.
    bool feasible = true;
    IncSolver *seps = NULL, *alns = NULL;
    IncSolver *sepnocsolv = NULL, *alnnocsolv = NULL;
    // On previous calls to this function, some constraint(s) may have been
    // marked as unsatisfiable. Here we clear any such flags before beginning
    // the new satisfiability tests.
    for (Constraints::iterator it=sepc.begin(); it!=sepc.end(); it++) {
        Constraint *c = *it;
        c->unsatisfiable = false;
    }
    for (Constraints::iterator it=alnc.begin(); it!=alnc.end(); it++) {
        Constraint *c = *it;
        c->unsatisfiable = false;
    }
    // Set variables to represent current node positions.
    updateVarsFromNodeRects();
    // Begin satisfiability tests:
    // 1. The Separation Constraint:
    seps = satisfy(sepv,sepc,feasible);
    // 2. The Alignment Constraint:
    if (feasible) alns = satisfy(alnv,alnc,feasible);
    // 3. Non-overlap in the dimension of the alignment constraint:
    if (feasible) {
        updateNodeRectsFromVars();
        recomputeEdgeShapes(alnd);
        alnnocs->generateSeparationConstraints(alnd,alnv,alnc,alnr);
        alnnocsolv = satisfy(alnv,alnc,feasible);
    }
    // 4. Non-Overlap in the dimension of the separation constraint:
    if (feasible) {
        updateNodeRectsFromVars();
        recomputeEdgeShapes(sepd);
        sepnocs->generateSeparationConstraints(sepd,sepv,sepc,sepr);
        sepnocsolv = satisfy(sepv,sepc,feasible);
    }
    // Now conclude based on whether all tests were satisfiable or not.
    if (!feasible) {
        // One of the tests failed.
        // Pop state twice to return to state before this function call.
        popState();
        popState();
        // Restore rectangle coordinates.
        popRectCoords();
        // Trash the new edge shape.
        gi2ei.erase(newRectIndex);
        alnnocs->removeShape(newRectIndex);
        auxRectToExtRS.erase(newRectIndex);
        m_extendedRS.pop_back();
    } else {
        // The passed OrderedAlignment is feasible. We will keep it.
#ifndef FULLSOLVE
        // So far we have only tried to satisfy the constraints.
        // Now we should actually find an optimal solution.
        alnnocsolv->solve();
        sepnocsolv->solve();
#endif
        // Accept the new node positions.
        updateNodeRectsFromVars();
        // Pop state just once to get rid of the NOCs, but keep the new ordered alignment.
        popState();
        // We do not need the other state and node coords that we saved.
        dropState();
        dropRectCoords();
    }
    // Clean up, and return a boolean saying whether the passed OrderedAlignment
    // was feasible (and hence applied) or not.
    delete seps;
    delete alns;
    delete sepnocsolv;
    delete alnnocsolv;
    return feasible;
}

/* Constructs a solver and attempts to solve the passed constraints on the passed vars.
 * Returns a bool saying whether the constraints were satisfiable.
 */
bool solve(Variables &vs, Constraints &cs)
{
    bool sat = false;
    IncSolver solv(vs,cs);
    try {
        solv.solve();
        sat = true;
    } catch (UnsatisfiedConstraint uc) {
        sat = false;
    }
    if (sat) {
        for (Constraints::iterator it=cs.begin(); it!=cs.end(); it++) {
            Constraint *c = *it;
            if (c->unsatisfiable) {
                sat = false;
                break;
            }
        }
    }
    return sat;
}

// Constructs a solver and attempts to satisfy the passed constraints on the
// passed vars. (Actually, does a full solve if FULLSOLVE is defined.)
// Sets the bool passed by reference according to whether it was
// possible to satisfy the constraints.
// Returns the solver.
vpsc::IncSolver *ACALayout3::satisfy(Variables &vs, Constraints &cs, bool &sat)
{
    IncSolver *solv = new IncSolver(vs,cs);
    try {
#ifdef FULLSOLVE
        solv->solve();
#else
        solv->satisfy();
#endif
        sat = true;
    } catch (UnsatisfiedConstraint uc) {
        sat = false;
    }
    for (Constraints::iterator it=cs.begin(); it!=cs.end(); it++) {
        Constraint *c = *it;
        if (c->unsatisfiable) {

            /*
            // DEBUG -----
            printf("unsat ac: %d, %d, %.2f, %d\n", c->left->id, c->right->id,
                   c->gap, c->equality);
            // -----------
            */

            sat = false;
            break;
        }
    }
    return solv;
}

bool ACALayout3::satisfiable(Solver solv)
{
    bool sat = true;
    try {
        solv.satisfy();
    } catch (UnsatisfiedConstraint uc) {
        sat = false;
    }
    return sat;
}

bool ACALayout3::isFeasible(OrderedAlignment *oa)
{
    return isFeasible(oa->edgeIndex,oa->sf);
}

bool ACALayout3::isFeasible(int j, ACASepFlag sf)
{
    // First check whether it is a bad separation.
    bool bs = badSeparation(j,sf);
    if (bs) return false;
    // Next check whether the alignment would create
    // any conflict with existing constraints, or an unremovable overlap.
    cola::Edge e = m_es.at(j);
    int src = e.first, tgt = e.second;
    bool co = createsConflict(src,tgt,sf);
    if (co) return false;
    return true;
}

bool ACALayout3::isFeasible(int l, int r, ACASepFlag sf)
{
    // First check whether it is a bad separation.
    bool bs = badSeparation(l,r,sf);
    if (bs) return false;
    // Next check whether the alignment would create
    // any conflict with existing constraints, or an unremovable overlap.
    bool co = createsConflictOrderKnown(l,r,sf);
    if (co) return false;
    return true;
}

// Say whether the proposed separation is a bad one.
bool ACALayout3::badSeparation(int j, ACASepFlag sf)
{
    // If allowed separations have been set, then check whether sf is allowed.
    if (m_allowedSeps.size()>0) {
        ACASepFlag allowed = m_allowedSeps.at(j);
        if ( (sf&allowed) != sf ) return true; // it is a bad separation
    }
    // If we are /not/ doing aggressive ordering, then
    // check if sf is ruled out for reversing existing order.
    if (!m_aggressiveOrdering) {
        cola::Edge e = m_es.at(j);
        int src = e.first, tgt = e.second;
        vpsc::Rectangle *rs = getRect(src), *rt = getRect(tgt);
        double dx = rt->getCentreX() - rs->getCentreX();
        double dy = rt->getCentreY() - rs->getCentreY();
        ACASepFlag currPos = vectorToSepFlag(dx,dy);
        bool conflict = propsedSepConflictsWithExistingPosition(sf,currPos);
        if (conflict) return true; // it is a bad separation
    }
    return false;
}

bool ACALayout3::badSeparation(int l, int r, ACASepFlag sf)
{
    // If we are /not/ doing aggressive ordering, then
    // first check if sf is ruled out for reversing existing order.
    //// Do aliasing
    //l = alias(l);
    //r = alias(r);
    if (!m_aggressiveOrdering) {
        vpsc::Rectangle *rs = getRect(l), *rt = getRect(r);
        double dx = rt->getCentreX() - rs->getCentreX();
        double dy = rt->getCentreY() - rs->getCentreY();
        ACASepFlag currPos = vectorToSepFlag(dx,dy);
        bool conflict = propsedSepConflictsWithExistingPosition(sf,currPos);
        if (conflict) return true; // it is a bad separation
    }
    return false;
}

bool ACALayout3::createsConflict(int src, int tgt, ACASepFlag sf)
{
    // Determine which shape is low and which is high in the dimension of interest.
    int lowIndex = sf==ACANORTH || sf==ACAWEST ? tgt : src;
    int highIndex = (lowIndex==tgt ? src : tgt);
    return createsConflictOrderKnown(lowIndex,highIndex,sf);
}

bool ACALayout3::createsConflictOrderKnown(int l, int r, ACASepFlag sf)
{
    // Save state.
    pushState();
    // Create a complete OrderedAlignment object for the proposed alignment.
    OrderedAlignment *oa = initOrdAlign(l,r,sf);
    completeOrdAlign(oa);
    // Add its vars and constraints.
    updateVarsAndCons(oa);
    // No longer need the oa.
    delete oa;
    // Add nonoverlap constraints between the edge to be aligned and all other nodes.
    // TODO

    // Try to satisfy the constraints in each dimension.
    bool xOkay = true, yOkay = true;
    vpsc::Solver xSolver(m_xvs,m_xcs);
    try {
        xSolver.satisfy();
    } catch (vpsc::UnsatisfiedConstraint uc) {
        xOkay = false;
    }
    // Don't bother with y-dim if x already not okay.
    if (xOkay) {
        vpsc::Solver ySolver(m_yvs,m_ycs);
        try {
            ySolver.satisfy();
        } catch (vpsc::UnsatisfiedConstraint uc) {
            yOkay = false;
        }
    }
    // Restore state before check.
    popState();
    // There was a conflict if either x or y was not okay.
    bool conflict = !xOkay || !yOkay;
    return conflict;
}



bool ACALayout3::createsOverlap(int src, int tgt, ACASepFlag sf)
{
    // Determine which shape is low and which is high in the dimension of interest.
    int lowIndex = sf==ACANORTH || sf==ACAWEST ? tgt : src;
    int highIndex = (lowIndex==tgt ? src : tgt);
    ACAFlag af = sepToAlignFlag(sf);
    // Determine the coordinates in the dimension of interest.
    vpsc::Rectangle *rLow = getRect(lowIndex), *rHigh = getRect(highIndex);
    double lowCoord = af==ACAHORIZ ? rLow->getCentreX() : rLow->getCentreY();
    double highCoord = af==ACAHORIZ ? rHigh->getCentreX() : rHigh->getCentreY();
    // Let L and H be the low and high shapes respectively.
    // We consider each node U which is already aligned with either L or H.
    // Any such node must have lower coord than L if it is connected to L, and
    // higher coord than H if it is connected to H. If either of those conditions
    // fails, then we predict overlap.
    bool overlap = false;
    for (int j = 0; j < m_n; j++) {
        if (j==lowIndex || j==highIndex) continue;
        vpsc::Rectangle *r = getRect(j);
        int lj = (*m_alignmentState)(lowIndex, j);
        int hj = (*m_alignmentState)(highIndex, j);
        if (lj&af || hj&af) {
            double z = af==ACAHORIZ ? r->getCentreX() : r->getCentreY();
            // low shape
            if ( lj&ACACONN && lowCoord < z ) {
                overlap = true; break;
            }
            // high shape
            if ( hj&ACACONN && z < highCoord ) {
                overlap = true; break;
            }
        }
    }
    return overlap;
}

bool ACALayout3::createsOverlapOrderKnown(int l, int r, ACASepFlag sf)
{
    ACAFlag af = sepToAlignFlag(sf);
    // Determine the coordinates in the dimension of interest.
    vpsc::Rectangle *rLow = getRect(l), *rHigh = getRect(r);
    double lowCoord = af==ACAHORIZ ? rLow->getCentreX() : rLow->getCentreY();
    double highCoord = af==ACAHORIZ ? rHigh->getCentreX() : rHigh->getCentreY();
    // Let L and H be the low and high shapes respectively.
    // We consider each node U which is already aligned with either L or H.
    // Any such node must have lower coord than L if it is connected to L, and
    // higher coord than H if it is connected to H. If either of those conditions
    // fails, then we predict overlap.
    bool overlap = false;
    for (int j = 0; j < m_n; j++) {
        if (j==l || j==r) continue;
        vpsc::Rectangle *R = getRect(j);
        int lj = (*m_alignmentState)(l, j);
        int hj = (*m_alignmentState)(r, j);
        if (lj&af || hj&af) {
            double z = af==ACAHORIZ ? R->getCentreX() : R->getCentreY();
            // low shape
            if ( lj&ACACONN && lowCoord < z ) {
                overlap = true; break;
            }
            // high shape
            if ( hj&ACACONN && z < highCoord ) {
                overlap = true; break;
            }
        }
    }
    return overlap;
}

/*
bool ACALayout::createsOverlap2(int j, ACASepFlag sf)
{
    cola::Edge e = m_es.at(j);
    int src = e.first, tgt = e.second;
    // Do aliasing
    src = alias(src);
    tgt = alias(tgt);
    EdgeOffset offset = getEdgeOffsetForCompassDirection(j,sf);
    double srcOffset = offset.first, tgtOffset = offset.second;
    vpsc::Dim dim = sepToAlignFlag(sf) == ACAHORIZ ? vpsc::XDIM : vpsc::YDIM;
    AlignedNodes *srcSet = getAlignmentSet(dim,src);
    AlignedNodes *tgtSet = getAlignmentSet(dim,tgt);
    AlignedNodes *a = srcSet->combineWithEdge(*tgtSet,src,srcOffset,tgt,tgtOffset,j);
    bool ans = a->thereAreOverlaps();
    delete a;
    return ans;
}
*/

/// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
/// Penalty evaluation

double ACALayout3::computePenalty(int j, ACASepFlag sf)
{
    double p = 0;
    cola::Edge e = m_es.at(j);
    int src = e.first, tgt = e.second;
    // Basic penalty:
    if (m_favourLongEdges) {
        // Length:
        p += lengthPenaltyForEdge(j);
    } else {
        // Deflection:
        p += deflectionForEdge(j,sf);
    }
    // Bend points:
    if (m_addBendPointPenalty) p += bendPointPenalty(src,tgt,sf);
    // Leaves:
    if (m_postponeLeaves) p += leafPenalty(src,tgt);
    //
    return p;
}

double ACALayout3::lengthPenaltyForEdge(int j)
{
    cola::Edge e = m_es.at(j);
    vpsc::Rectangle *src = m_rs.at(e.first), *tgt = m_rs.at(e.second);
    double sx=src->getCentreX(), sy=src->getCentreY(), tx=tgt->getCentreX(), ty=tgt->getCentreY();
    double dx=tx-sx, dy=ty-sy;
    double l=sqrt(dx*dx+dy*dy);
    return 1 - l/m_lengthUpperBound;
    // FIXME: We really should update the "length upper bound" on each iteration, and make
    // sure that it actually is an upper bound on all edge lengths. We need to do this so
    // that this score can be between 0 and 1.
}

/* Compute a score in [0.0, 1.0] measuring how far edge j is deflected
 * from horizontal or vertical, depending on the passed alignment flag.
 *
 * If t is the angle the edge makes with the positive x-axis, then the
 * score we return is sin^2(t) for horizontal alignments, and cos^2(t)
 * for vertical.
 *
 * So smaller deflection scores mean edges that are closer to axis-aligned.
 */
double ACALayout3::deflectionForEdge(int j, ACASepFlag sf)
{
    cola::Edge e = m_es.at(j);
    int src = e.first, tgt = e.second;
    // Do aliasing
    src = alias(src);
    tgt = alias(tgt);
    vpsc::Rectangle *s = getRect(src), *t = getRect(tgt);
    // Start with centre points of the nodes.
    double sx=s->getCentreX(), sy=s->getCentreY(), tx=t->getCentreX(), ty=t->getCentreY();
    // Check offsets.
    EdgeOffset offset = getEdgeOffsetForCompassDirection(j,sf);
    if (sf&ACAEASTWEST) {
        // It is a horizontal alignment, so the offset is to the y-dimension.
        sy += offset.first;
        ty += offset.second;
    } else {
        // It is a vertical alignment, so the offset is to the x-dimension.
        sx += offset.first;
        tx += offset.second;
    }
    return deflection(sx,sy,tx,ty,sf);
}

double ACALayout3::deflection(double sx, double sy, double tx, double ty, ACASepFlag sf)
{
    double dx = tx-sx, dy = ty-sy;
    double dx2 = dx*dx, dy2 = dy*dy;
    double l = dx2 + dy2;
    double dfl = sf==ACAWEST || sf==ACAEAST ? dy2/l : dx2/l;
    return dfl;
}

/**
 * If the nodes of indices u and v were to be aligned so that sf gave the direction
 * from u to v, say by what angle (in radians) the current vector from u to v would
 * rotate. The return value is between -pi and +pi.
 */
double ACALayout3::changeInAngle(int u, int v, ACASepFlag sf)
{
    vpsc::Rectangle *ru = m_rs.at(u), *rv = m_rs.at(v);
    double xu=ru->getCentreX(), yu=ru->getCentreY(), xv=rv->getCentreX(), yv=rv->getCentreY();
    double dx=xv-xu, dy=yv-yu;
    double a = atan2(dy,dx);
    // a now measures angle away from EAST.
    double pi = 3.141593;
    switch (sf) {
    case ACANORTH:
        a = a <= pi/2 ? a + pi/2 : a - 3*pi/2; break;
    case ACAWEST:
        a = a <= 0 ? a + pi : a - pi; break;
    case ACASOUTH:
        a = a <= -pi/2 ? a + 3*pi/2 : a - pi/2; break;
    default:
        break;
    }
    return a;
}

double ACALayout3::bendPointPenalty(int src, int tgt, ACASepFlag sf)
{
    double penalty = 0;
    ACAFlag af = sepToAlignFlag(sf);
    ACAFlag op = af==ACAHORIZ ? ACAVERT : ACAHORIZ;
    std::set<int> &deg2Nodes = m_useNonLeafDegree ? m_nldeg2Nodes : m_deg2Nodes;
    std::multimap<int,int> &nbrs = m_useNonLeafDegree ? m_nlnbrs : m_nbrs;
    // First check whether src would be made into a bendpoint.
    if (deg2Nodes.count(src)!=0) {
        // Find neighbour j of src which is different from tgt, by iterating over nbrs of src.
        int j = 0;
        std::pair< std::multimap<int,int>::iterator, std::multimap<int,int>::iterator > range;
        range = nbrs.equal_range(src);
        for (std::multimap<int,int>::iterator it=range.first; it!=range.second; ++it) {
            j = it->second;
            if (j != tgt) break;
        }
        // Now check if they are aligned in the opposite dimension. If so, add penalty.
        int as = (*m_alignmentState)(src,j);
        if (as & op) penalty += BP_PENALTY;
    }
    // Now check whether tgt would be made into a bendpoint.
    if (deg2Nodes.count(tgt)!=0) {
        // Find neighbour j of tgt which is different from src, by iterating over nbrs of tgt.
        int j = 0;
        std::pair< std::multimap<int,int>::iterator, std::multimap<int,int>::iterator > range;
        range = nbrs.equal_range(tgt);
        for (std::multimap<int,int>::iterator it=range.first; it!=range.second; ++it) {
            j = it->second;
            if (j != src) break;
        }
        // Now check if they are aligned in the opposite dimension. If so, add penalty.
        int as = (*m_alignmentState)(tgt,j);
        if (as & op) penalty += BP_PENALTY;
    }
    return penalty;
}

double ACALayout3::leafPenalty(int src, int tgt)
{
    double penalty = LEAF_PENALTY;
    return m_leaves.count(src)!=0 || m_leaves.count(tgt)!=0 ? penalty : 0;
}

EdgeOffset ACALayout3::getEdgeOffsetForCompassDirection(int j, ACASepFlag sf)
{
    EdgeOffset offset(0,0);
    // If offsets have been specified for this sep flag, then retrieve
    // the offset of the given index.
    std::map<ACASepFlag,EdgeOffsets>::iterator it = m_edgeOffsets.find(sf);
    if (it!=m_edgeOffsets.end()) {
        EdgeOffsets offsets = it->second;
        offset = offsets.at(j);
    }
    return offset;
}

} // namespace cola
