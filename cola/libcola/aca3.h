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

#ifndef ACA3_H
#define ACA3_H

#include <vector>

#include "libcola/cola.h"

namespace cola {

#ifndef SWIG
void testACA();
#endif

template<typename T>
struct Matrix2d
{
    int rows, cols;
    std::vector<T> data;
    Matrix2d() : rows(0), cols(0) {}
    Matrix2d(int rows, int cols) : rows(rows), cols(cols), data(rows*cols)
    { }

    T operator()(int i, int j) const
    {
        assert(i < rows);
        assert(j < cols);
        return data[i*cols+j];
    }
    T& operator()(int i, int j)
    {
        assert(i < rows);
        assert(j < cols);
        return data[i*cols+j];
    }

    std::string toString() {
        std::string s = "";
        s += "\n  ";
        char buffer [10];
        for (int j=0; j<cols; j++) {
            sprintf(buffer," %2d",j);
            s += std::string(buffer);
        }
        for (int i=0; i<rows; i++) {
            s += "\n";
            sprintf(buffer,"%2d",i);
            s += std::string(buffer);
            for (int j=0; j<cols; j++) {
                sprintf(buffer," %2d",data[i*cols+j]);
                s += std::string(buffer);
            }
        }
        return s;
    }

};

enum ACAFlag {
    ACAHORIZ = 1,
    ACAVERT  = 2,
    ACADELIB = 4,
    ACACONN  = 8
};

enum ACASepFlag {
    ACANOSEP      =  0,

    ACANORTH      =  1,
    ACAEAST       =  2,
    ACASOUTH      =  4,
    ACAWEST       =  8,

    ACANORTHEAST  =  3,
    ACASOUTHEAST  =  6,
    ACANORTHWEST  =  9,
    ACASOUTHWEST  = 12,

    ACANORTHSOUTH =  5,
    ACAEASTWEST   = 10,

    ACANOTNORTH   = 14,
    ACANOTEAST    = 13,
    ACANOTSOUTH   = 11,
    ACANOTWEST    =  7,

    ACAALLSEP     = 15
};

enum ACAOverlapPrevention {
    ACAOPNONE,
    ACAOPCENTREALIGN,
    ACAOPWITHOFFSETS
};

/*
 * For SWIG compatibility we add a kind of superfluous struct that
 * holds a list of ACASepFlags and allows the flags to be added
 * to this list using a dedicated method.
 */
struct ACASepFlagsStruct {
    std::vector<ACASepFlag> sepFlags;

    void addFlag(ACASepFlag flag)
    {
        sepFlags.push_back(flag);
    }
};

struct OrderedAlignment {
    OrderedAlignment() :
        af(ACACONN), sf(ACANOSEP), left(-1), right(-1),
        offsetLeft(0), offsetRight(0),
        separation(NULL), alignment(NULL), edgeIndex(-1) {}
    ACAFlag af;
    ACASepFlag sf;
    vpsc::Dim dim;
    int left;
    int right;
    double offsetLeft;
    double offsetRight;
    cola::SeparationConstraint* separation;
    cola::AlignmentConstraint* alignment;
    int edgeIndex;
    double penalty;
};

typedef std::vector<OrderedAlignment*> OrderedAlignments;

struct NodeAlignment {
    NodeAlignment(int r, int n, ACASepFlag d, int edgeIndex = -1) :
        rootIndex(r), nbrIndex(n), direction(d), assocEdge(edgeIndex) {}
    int rootIndex;
    int nbrIndex;
    ACASepFlag direction;
    int assocEdge; // in case there is an associated edge
};

struct EdgeNode {
    int edgeIndex;
    int src;
    int tgt;
    int astate;
};

struct ProjectionResult {
    int errorLevel;
    std::string unsatinfo;
};

typedef std::vector<NodeAlignment> NodeAlignments;

typedef std::pair<double,double> EdgeOffset;
typedef std::vector<EdgeOffset> EdgeOffsets;
typedef std::vector<ACASepFlag> ACASepFlags;

enum EventType {
    OPEN, CLOSE, LOCAL
};

struct Event {
    Event(EventType t) : m_type(t) {}
    EventType m_type;
    double m_pos;
    double m_lowerBound;
    double m_upperBound;
    int m_index;
};

struct RootedEdge{
    RootedEdge(int j0, int k0, double x0, double y0) :
        j(j0), k(k0), x(x0), y(y0) {
        angle = atan2(y,x);
    }
    int j; // index of edge
    int k; // index of distal node
    double x; // x-coord of endpt of edge which is /not/ the root
    double y; // y-coord of same pt
    double angle;
};

ACASepFlag negateSepFlag(ACASepFlag sf);

ACAFlag sepToAlignFlag(ACASepFlag sf);

ACAFlag perpAlignFlag(ACAFlag af);

ACASepFlag vectorToSepFlag(double dx, double dy);

// Create array of all four cardinals, in counterclockwise order,
// starting from the given one.
std::vector<ACASepFlag> ccwCardinalsFrom(ACASepFlag start);

// Create array of all four cardinals, in clockwise order,
// starting from the given one.
std::vector<ACASepFlag> cwCardinalsFrom(ACASepFlag start);

bool propsedSepConflictsWithExistingPosition(ACASepFlag pro, ACASepFlag ex);

bool propsedSepConflictsWithExistingSepCo(ACASepFlag pro, ACASepFlag ex);

bool sortRootedEdges(const RootedEdge &lhs, const RootedEdge &rhs);

bool sortOrdAlignsByPenalty(const OrderedAlignment *lhs, const OrderedAlignment *rhs);

// FIXME: Should be placed in a more natural location if we refactor the library.
ProjectionResult projectOntoCCs(vpsc::Dim dim, vpsc::Rectangles &rs, cola::CompoundConstraints ccs,
                                bool preventOverlaps, int accept=0);
ProjectionResult solve(vpsc::Variables &vs, vpsc::Constraints &cs, vpsc::Rectangles &rs);

// -----------------------------------------------------------------------------------

/**
 * @brief Implements the Adaptive Constrained Alignment (ACA) algorithm.
 *
 * See
 * Steve Kieffer, Tim Dwyer, Kim Marriott, and Michael Wybrow.
 * "Incremental grid-like layout using soft and hard constraints." In Graph
 * Drawing 2013, pp. 448-459. Springer International Publishing, 2013.
 */
class ACALayout3 {
public:
    /**
     * @brief Constructs an adaptive constrained alignment layout instance.
     *
     * Parameters are the same as for the ConstrainedFDLayout constructor,
     * with the addition of a vector of constraints passed by reference.
     *
     * If the vector of constraints is non-empty, these constraints will be
     * applied throughout the ACA process, and the new constraints created
     * by ACA will not conflict with any of these.
     *
     * Conversely, if no constraints are passed, the empty vector is still
     * used to store the new constraints created by ACA.
     *
     * @param[in] rs  Bounding boxes of nodes at their initial positions.
     * @param[in] es  Simple pair edges, giving indices of the start and end
     *                nodes in rs.
     * @param[in] ccs  Vector of any pre-existing constraints, and holder where
     *                 new constraints created by ACA will be recorded.
     * @param[in] idealLength  A scalar modifier of ideal edge lengths in
     *                         eLengths or 1 if no ideal lengths are specified..
     * @param preventOverlaps  Causes non-overlap constraints to be generated
     *                          for all rectangles, if it is set to true.
     * @param[in] eLengths  Individual ideal lengths for edges.
     *                      The actual ideal length used for the ith edge is
     *                      idealLength*eLengths[i], or if eLengths is NULL a
     *                      then just idealLength is used (i.e., eLengths[i]
     *                      is assumed to be 1).
     * @param[in] done  A test of convergence operation called at the end of
     *                  each iteration (optional).  If not given, uses a
     *                  default TestConvergence object.
     * @param[in] preIteration  An operation called before each iteration
     *                          (optional).
     */
    ACALayout3(
        const vpsc::Rectangles& rs,
        const std::vector<cola::Edge>& es,
        CompoundConstraints& ccs,
        const double idealLength,
        const bool preventOverlaps,
        const EdgeLengths& eLengths = StandardEdgeLengths,
        TestConvergence* doneTest = NULL,
        PreIteration* preIteration=NULL);

    // To prevent C++ objects from being destroyed in garbage collected languages
    // when the libraries are called from SWIG, we hide the declarations of the
    // destructors and prevent generation of default destructors.
    #ifndef SWIG
    ~ACALayout3(void);
    #endif
    /**
     * @brief Creates alignments.
     *
     * This is the main functionality of ACA. This function should be called
     * on an existing layout in order to greedily align edges until any further
     * alignments would create edge overlaps.
     *
     * If the graph does not have an initial layout already, then the 'layout'
     * function may be called instead.
     */
    void createAlignments(void);
    /**
     * @brief Creates one alignment.
     *
     * Call this function instead of createAlignments in order to create just
     * one alignment and then stop. The return value is true if and only if a
     * new alignment was actually created.
     *
     * Thus, repeatedly calling this function until it returns false achieves
     * the exact same result as simply calling createAlignments.
     */
    bool createOneAlignment(void);
    /**
     * @brief Creates all the requested alignments, or none if any is infeasible.
     *
     * @return true if all alignments are successfully applied, else false.
     */
    bool applyOAsAllOrNothing(OrderedAlignments oas);
    /**
     * @brief Do an initial layout, and then create alignments.
     *
     * This is a convenience function which first does a constrained force-directed
     * layout of the given graph, and then calls the 'createAlignments' function.
     */
    void layout(void);
    /**
     * @brief Do an FD layout with overlap prevention, then stop.
     */
    void removeOverlaps(void);
    /**
     * @brief Run layout with current constraints, and with or without overlap
     * prevention, as per the current settings.
     */
    void layoutWithCurrentConstraints(void);

    void outputInstanceToSVG(std::string filename);

    cola::ConstrainedFDLayout *getFDLayout(void);

    // Configuration methods:

    /**
     * @brief Control whether we avoid making bend points.
     *
     * We refer to a node of degree 2 as a "bend point" when one of its
     * edges has been aligned horizontally and the other vertically.
     *
     * The default value of addBendPointPenalty is true. In this case a penalty
     * score is added when choosing the next alignment in order to postpone
     * creating bend points until no other choices remain.
     *
     * If set to false then there is no penalty score to postpone the creation
     * of bend points.
     *
     * When there is both bend point penalty and leaf penalty (see below), then
     * bend points will be created before leaf edges are aligned. This can be
     * reversed by altering the BP_PENALTY and LEAF_PENALTY constants.
     */
    void addBendPointPenalty(bool b);
    /**
     * @brief Prefer long edges instead of ones that are close to aligned.
     *
     * This defaults to 'false', in which case ACA prefers to align edges that
     * are almost aligned already. When set to 'true' it will instead choose
     * the longest edges first.
     */
    void favourLongEdges(bool b);
    /**
     * @brief Say whether alignment of leaf edges should be saved for last.
     *
     * The default value is true.
     */
    void postponeLeaves(bool b);
    /**
     * @brief Say whether leaves should be counted when computing node degrees.
     *
     * The default value is true.
     *
     * This setting matters only if addBendPointPenalty is set to true.
     * In that case, if useNonLeafDegree is also true then the nodes identified
     * as potential bend points will be those having exactly 2 /non-leaf/ neighbours.
     *
     * When there is both leaf penalty and bend point penalty (see above), then
     * bend points will be created before leaf edges are aligned. This can be
     * reversed by altering the BP_PENALTY and LEAF_PENALTY constants.
     */
    void useNonLeafDegree(bool b);
    /**
     * @brief Say whether alignment choices should alternate with layout steps.
     *
     * The default value of allAtOnce is false. In this case, after each new
     * alignment is chosen, the graph is again laid out before choosing the
     * next one.
     *
     * If you set allAtOnce to true, then all the alignments will be chosen based
     * on the initial layout, and then they will all be applied at once.
     */
    void allAtOnce(bool b);
    /**
     * @brief Say whether to consider changing orthogonal ordering of nodes.
     *
     * The default value is false. In that case, consider a pair of nodes
     * u, v where v currently lies to the southeast of u. Then when ACA
     * considers aligning u and v it will consider /only/ putting v east of
     * u, and putting v south of u. In other words, it will /not/ consider
     * reversing their current ordering in either dimension.
     *
     * In the same example, if you set aggressiveOrdering to true, then ACA
     * will also consider putting v north and west of u.
     *
     * In the exceptional case of a node v lying, say, precisely east of a node
     * u despite not being constrained to that alignment, then ACA will consider
     * placing v east, north, and south of u even with aggressiveOrdering set
     * to false. (But it will consider west only with it set to true.)
     */
    void aggressiveOrdering(bool b);
    /**
     * @brief Say which test to use when checking whether an alignment would
     * make an edge overlap a node or another edge.
     *
     * The default value is ACAOPCENTREALIGN, which should be used if no
     * edges have any offsets.
     *
     * Use ACAOPWITHOFFSETS for our more sophisticated check, which considers
     * the offsets that you have assigned to edges (if any).
     * If you want to use this option, you MUST set it immediately after
     * constructing the ACALayout object.
     * Also you should set this value only ONCE per ACALayout object.
     *
     * Set to ACAOPNONE for no overlap checking.
     */
    void overlapPrevention(ACAOverlapPrevention p);
    /**
     * @brief Specify that certain edges are never to be aligned.
     *
     * The number of booleans must equal the number of edges in the graph.
     * Entry j should be 'true' if you want edge j to be ignored (never aligned);
     * 'false' otherwise.
     */
    void ignoreEdges(std::vector<bool> ignore);
    /**
     * @brief Say that certain nodes may be crossed by edges.
     *
     * When using the ACAOPWITHOFFSETS option, a potential alignment will /not/ be
     * marked as creating an edge-node overlap if for all indices i such that the
     * alignment would make an edge overlap node i, the ith entry in the vector passed
     * to this function is true.
     */
    void ignoreNodesForOPWithOffsets(std::vector<bool> ignore);
    /**
     * @brief Set certain nodes to be used in place of others.
     *
     * Pass a map m. Then for each computation that involves a node u, we will check
     * whether u is a key in m, and if so will use m(u) instead.
     */
    void setNodeAliases(std::map<int,int> aliases);
    /**
     * @brief Say how to offset nodes when edges are aligned in a certain direction.
     *
     * The number of offsets must equal the number of edges in the graph.
     * For any edge e=(s,t), let o=(ds,dt) be the corresponding offset. Then if ACA aligns edge e
     * so that sf is the separation from s to t, then s will be offset from the alignment
     * guideline by ds, and t by dt.
     *
     * If you do not set any offsets then offsets of zero will be used.
     */
    void setAlignmentOffsetsForCompassDirection(ACASepFlag sf, EdgeOffsets offsets);
    /**
     * @brief Say which separations are allowed for the source and target of each edge.
     *
     * The number of separation flags must equal the number of edges in the graph.
     * For any edge e=(s,t), let f be the corresponding flag. Then when ACA considers
     * aligning edge e it will only consider the directions whose bits are set in f.
     *
     * If you do not set allowed separations then all are considered to be allowed.
     *
     * Note that if aggressiveOrdering is false then even if you set ACAALLSEP as
     * allowed direction, still only the two "natural" directions will be tried.
     * (See aggressiveOrdering().)
     */
    void setAllowedDirections(ACASepFlags seps);
    /**
     * @brief Method defined for SWIG compatibility, see overloaded variant for details.
     */
    void setAllowedDirections(ACASepFlagsStruct seps);
    /**
     * @brief sets the cluster hierarchy of the underlying FDLayout.
     */
    void setClusterHierarchy(cola::RootCluster *rc);
    /**
     * @brief Check whether an edge is aligned.
     *
     * Pass the index of the edge. The returned boolean says whether ACA aligned this edge.
     */
    bool edgeIsAligned(int j);
    /**
     * @brief Stop non-overlap constraints from being generated between certain nodes.
     */
    void addGroupOfNonOverlapExemptRectangles(std::vector<unsigned> rs);

    // Check in constant time whether edge is aligned.
    // TODO: Get rid of the other method, whose time is logarithmic
    // in the number of currently aligned edges.
    bool edgeIsAlignedConstTime(int j);

    bool nodesAreAligned(int i, int j);

    void layoutPeriod(unsigned p);
    void doFinalLayout(bool b);

    std::vector<OrderedAlignment*> orderedAlignments(void);
    void addOrderedAlignments(OrderedAlignments oas);
    OrderedAlignment *initOrdAlign(int l, int r, ACASepFlag sf, int edgeIndex=-1);

    // Experimental:
    std::string configureDeg4Nodes(void);
    std::string configureDeg3Nodes(void);
    void replaceOrderedAlignments(std::vector<OrderedAlignment*> oas);

    // For debugging:
    cola::UnsatisfiableConstraintInfos *unsatX(void);
    cola::UnsatisfiableConstraintInfos *unsatY(void);
    OrderedAlignment *mostRecentOA(void);
    std::string writeAlignmentTable(void);
    std::string aStateBeforeChop;

private:
    /**
     * Used by the constructor to compute the degrees of nodes in the graph, and
     * determine which are the leaves, and which are the nodes of degree 2, both
     * in the sense of ordinary degree and non-leaf degree.
     */
    void computeDegrees(void);
    /**
     * Used by the constructor to convert the incoming cola constraints into vpsc
     * constraints, and the generate any additional variables that are needed by
     * those constraints.
     */
    void generateVPSCConstraints(void);
    /**
     * Used only during initialisation of the state tables, to handle any additional
     * variables generated by the incoming cola constraints.
     */
    int adjustVarNumForExtraVars(vpsc::Dim dim, int k);
    /**
     * Used by the constructor to initialise the state tables, computing the transitive
     * closure of all incoming alignments and separations, based on the vpsc constraints
     * to which the incoming cola constraints compile.
     */
    void initStateTables(void);
    /**
     * Record the specified alignment between rectangles i and j.
     * Also record all additional alignments arising from the transitive
     * closure.
     *
     * The numCols variable is only there to manage the state table
     * initialisation process, where we have to deal with the extra variables
     * generated by the incoming constraints.
     */
    void recordAlignmentWithClosure(int i, int j, ACAFlag af, int numCols = 0);

    void initNOCs(void);

    std::set<int> getNodesByDegree(int d);
    std::vector<int> cyclicOrderedEdgesAroundNode(int i);
    std::vector<int> cyclicOrderedNbrsAroundNode(int i);
    std::vector<RootedEdge> cyclicOrderedRootedEdgesAroundNode(int i);

    bool acaLoopOnce(void);
    void acaLoopOneByOne(void);
    void acaLoopAllAtOnce(void);

    bool layoutIfAppropriate(void);
    cola::CompoundConstraints writeAllVPSCConstraintsAsCompound(void);
    vpsc::Rectangles properAndAuxRects(void);

    std::string createFeasibleNodeAlignments(NodeAlignments nas, std::string debugName="");

    void updateStateTables(OrderedAlignment *oa);
    void updateVarsAndCons(OrderedAlignment *oa);

    std::pair<double,double> getEdgeShapeDims(int gi, vpsc::Dim dim);
    vpsc::Rectangle *makeRectForOA(OrderedAlignment *oa);
    vpsc::Rectangle *makeRectForEdge(int j, vpsc::Dim dim);
    void updateRectForEdge(vpsc::Rectangle *R, int j, vpsc::Dim dim);
    void recomputeEdgeShapes(vpsc::Dim dim);
    void updateNodeRectsFromVars(void);
    void updateVarsFromNodeRects(void);
    void generateNonoverlapVarsAndCons(vpsc::Dim dim);
    void pushState(void);
    void popState(void);
    void dropState(void);
    void pushRectCoords(void);
    void popRectCoords(void);
    void dropRectCoords(void);
    void trashNewEdgeShapesAccordingToStateStack(void);
    std::set<unsigned> exemptionSetForEdge(int j);

    OrderedAlignment *chooseOA(void);
    OrderedAlignment *chooseOA2(void);
    bool allOrNothing(OrderedAlignments oas);
    bool applyIfFeasible(OrderedAlignment *oa);
    bool satisfiable(vpsc::Solver solv);
    vpsc::IncSolver *satisfy(vpsc::Variables &vs, vpsc::Constraints &cs, bool &sat);
    // In the following methods, the separation flag always means the direction from src to tgt.
    bool isFeasible(int j, ACASepFlag sf);
    bool isFeasible(int l, int r, ACASepFlag sf);
    bool isFeasible(OrderedAlignment *oa);
    OrderedAlignment *initOrdAlign(int j, ACASepFlag sf);
    void completeOrdAlign(OrderedAlignment *oa);
    bool badSeparation(int j, ACASepFlag sf);
    bool badSeparation(int l, int r, ACASepFlag sf);

    bool createsOverlap(int src, int tgt, ACASepFlag sf);
    bool createsOverlapOrderKnown(int l, int r, ACASepFlag sf);
    //bool createsOverlap2(int j, ACASepFlag sf);

    bool createsConflict(int src, int tgt, ACASepFlag sf);
    bool createsConflictOrderKnown(int l, int r, ACASepFlag sf);

    double computePenalty(int j, ACASepFlag sf);
    double lengthPenaltyForEdge(int j);
    double deflectionForEdge(int j, ACASepFlag sf);
    double deflection(double sx, double sy, double tx, double ty, ACASepFlag sf);
    double changeInAngle(int u, int v, ACASepFlag sf);
    double bendPointPenalty(int src, int tgt, ACASepFlag sf);
    double leafPenalty(int src, int tgt);

    NodeAlignment chooseNearestAlignment(int rootIndex, std::vector<int> nbrIndices,
                                                     ACASepFlag availableCardinals = ACAALLSEP);
    NodeAlignment chooseNearestAlignment(int rootIndex, std::vector<int> nbrIndices,
                                                     ACASepFlags availableCardinals);

    NodeAlignment chooseNearestAlignment(int rootIndex, std::vector<RootedEdge> rootedEdges,
                                                     ACASepFlag availableCardinals = ACAALLSEP);
    NodeAlignment chooseNearestAlignment(int rootIndex, std::vector<RootedEdge> rootedEdges,
                                                     ACASepFlags availableCardinals);

    EdgeOffset getEdgeOffsetForCompassDirection(int j, ACASepFlag sf);
    int alias(int i);
    vpsc::Rectangle *getRect(int i, bool doAlias=false);

    // The penalty values determine the order in which certain types of
    // alignments will be created. (See above.)
    // The PENALTY_BOUND must be greater than the sum of the other penalty
    // values plus 1 (the maximum possible deflection score).

    static const double BP_PENALTY = 2; // can be applied twice for one edge
    static const double LEAF_PENALTY = 5;
    static const double PENALTY_BOUND = 20;

    static const double EDGE_SHAPE_HALF_THICKNESS = 4;
    static const double EDGE_SHAPE_BUFFER = 1;

    int m_n; // number of nodes
    int m_m; // number of edges
    int m_numExtraXVars;
    int m_numExtraYVars;
    vpsc::Rectangles m_rs;
    std::vector<cola::Edge> m_es;
    cola::CompoundConstraints &m_ccs;
    cola::RootCluster *m_rc;

    std::vector<bool> m_ignoreEdge;
    std::vector<bool> m_ignoreNodeForOPWithOffsets;
    std::map<ACASepFlag,EdgeOffsets> m_edgeOffsets;
    ACASepFlags m_allowedSeps;
    std::map<int,int> m_nodeAliases;

    vpsc::Constraints m_xEqCs;
    vpsc::Constraints m_xIneqCs;
    vpsc::Constraints m_yEqCs;
    vpsc::Constraints m_yIneqCs;
    vpsc::Variables m_xvs;
    vpsc::Variables m_yvs;

    vpsc::Constraints m_xcs;
    vpsc::Constraints m_ycs;

    vpsc::Rectangles m_xrs;
    vpsc::Rectangles m_yrs;

    cola::NonOverlapConstraints *m_xnocs;
    cola::NonOverlapConstraints *m_ynocs;

    vpsc::Rectangles m_extendedRS;
    std::map<int,int> m_xAuxRectIndexInExtendedRS;
    std::map<int,int> m_yAuxRectIndexInExtendedRS;

    double m_idealLength;
    bool m_preventOverlaps;
    EdgeLengths m_edgeLengths;
    TestConvergence *m_doneTest;
    PreIteration *m_preIteration;
    double m_nodePadding;

    // Configuration:
    bool m_addBendPointPenalty;
    bool m_favourLongEdges;
    bool m_postponeLeaves;
    bool m_useNonLeafDegree;
    bool m_allAtOnce;
    bool m_aggressiveOrdering;
    ACAOverlapPrevention m_overlapPrevention;

    std::multimap<int,int> m_incidentEdges; // map node index to indices of incident edges
    std::multimap<int,int> m_nlincidentEdges; // map node index to indices of incident edges
    std::multimap<int,int> m_nbrs; // neighbours
    std::multimap<int,int> m_nlnbrs; // non-leaf neighbours
    std::set<int> m_leaves; // indices of rectangles that are leaves
    std::set<int> m_deg2Nodes; // degree-2 nodes w.r.t. ordinary nbrs
    std::set<int> m_nldeg2Nodes; // degree-2 nodes w.r.t. only non-leaf neighbours

    Matrix2d<int> *m_alignmentState;
    std::vector<OrderedAlignment*> m_ordAligns;
    std::set<int> m_alignedEdges;

    cola::ConstrainedFDLayout *m_fdlayout;
    cola::UnsatisfiableConstraintInfos *m_unsatX;
    cola::UnsatisfiableConstraintInfos *m_unsatY;

    double m_lengthUpperBound;

    std::vector<unsigned> m_sizeStack;
    std::vector<double> m_rectXStack;
    std::vector<double> m_rectYStack;
    std::map<int,int> m_edgeIndexToGuidelineIndex;
    std::map<int,int> m_guidelineIndexToEdgeIndex;

    std::map<int,int> m_xGuidelineIndexToEdgeIndex;
    std::map<int,int> m_yGuidelineIndexToEdgeIndex;

    bool m_didLayoutForLastAlignment;
    bool m_doFinalFDLayout;

    std::vector < std::vector<unsigned> > m_nocExemptRects;
    NonOverlapConstraintExemptions *m_nocExemptions;
    bool m_nocsInitialised;
    // Map each rect index to the set of all rect indices with which it is
    // exempt from non-overlap constraints:
    std::multimap<unsigned,unsigned> m_nocExemptionSets;

    unsigned m_layoutPeriod;
};

} // namespace cola

#endif // ACA3_H
