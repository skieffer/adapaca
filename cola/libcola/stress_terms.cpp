/*
 * vim: ts=4 sw=4 et tw=0 wm=0
 *
 * libcola - A library providing force-directed network layout using the
 *           stress-majorization method subject to separation constraints.
 *
 * Copyright (C) 2006-2008  Monash University
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
*/

#include "libcola/unused.h"
#include "libcola/sparse_matrix.h"
#include "libcola/stress_terms.h"

namespace cola {

StressTerm::StressTerm(double weight)
{
    this->weight = weight;
}

//-----------------------------------------------------------------------------
// SoftDistribution
//-----------------------------------------------------------------------------

void SoftDistribution::addIndexPair(unsigned u, unsigned v)
{
    std::pair<unsigned, unsigned> p;
    p.first = u;
    p.second = v;
    indexPairs.push_back(p);
}

std::vector<double> SoftDistribution::seps(std::valarray<double> X, std::valarray<double> Y)
{
    std::vector<double> seps;
    int np = indexPairs.size();
    for (int i = 0; i < np; i++) {
        std::pair<unsigned, unsigned> p = indexPairs[i];
        unsigned u = p.first, v = p.second;
        double sep = dim==vpsc::XDIM ? X[v] - X[u] : Y[v] - Y[u];
        seps.push_back(sep);
    }
    return seps;
}

//-----------------------------------------------------------------------------
// SoftDistributionStressTerm
//-----------------------------------------------------------------------------

SoftDistributionStressTerm::SoftDistributionStressTerm(double weight) :
    StressTerm(weight) {}

void SoftDistributionStressTerm::addSoftDistrubtion(SoftDistribution *sd)
{
    sds.push_back(sd);
}

double SoftDistributionStressTerm::computeStress(unsigned n,
                                          std::valarray<double> X, std::valarray<double> Y,
                                          unsigned short **G, double **D)
{
    COLA_UNUSED(n);
    COLA_UNUSED(G);
    COLA_UNUSED(D);

    double stress = 0;
    // Sum the contributions from each of the soft distributions.
    for (std::vector<SoftDistribution*>::iterator it=sds.begin(); it!=sds.end();++it) {
        SoftDistribution *sd = *it;
        // Compute the average separation.
        int np = sd->indexPairs.size();
        std::vector<double> seps = sd->seps(X, Y);
        double sum = 0;
        for (int i = 0; i < np; i++) sum += seps[i];
        double avg = sum/np;
        // Sum the squared displacements from the average.
        double totalSqDisp = 0;
        for (int i = 0; i < np; i++) {
            double disp = seps[i] - avg;
            totalSqDisp += disp*disp;
        }
        stress += totalSqDisp;
    }
    return weight*stress;
}

void SoftDistributionStressTerm::computeForces(unsigned n,
                                                 std::valarray<double> X, std::valarray<double> Y,
                                                 unsigned short **G, double **D,
                                                 vpsc::Dim dim, SparseMap &H,
                                                 std::valarray<double> &g)
{
    COLA_UNUSED(n);
    COLA_UNUSED(G);
    COLA_UNUSED(D);

    for (std::vector<SoftDistribution*>::iterator it=sds.begin(); it!=sds.end();++it) {
        SoftDistribution *sd = *it;
        if (sd->dim == dim) continue; // Skip the distributions that are not in the right dimension.
        std::vector<double> seps = sd->seps(X, Y);
        double w = weight;
        int np = sd->indexPairs.size();
        for (int i = 0; i < np; i++) {
            double d = seps[i];
            std::pair<unsigned, unsigned> p = sd->indexPairs[i];
            unsigned u = p.first, v = p.second;
            double wd = w*d;
            g[v] += wd;
            H(v,u) -= w;
            H(v,v) += w;
            g[u] -= wd;
            H(u,v) -= w;
            H(u,u) += w;
        }
    }
}

} // namespace cola










































