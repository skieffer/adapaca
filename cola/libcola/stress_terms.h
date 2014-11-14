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

#ifndef _STRESS_TERMS_H
#define _STRESS_TERMS_H

#include <vector>
#include <valarray>

#include "libvpsc/rectangle.h"

namespace cola {

struct SparseMap;

/**
 * @brief An abstract base class for all auxiliary stress terms.
 *
 * A stress term is a special term to be added to the stress and the
 * forces when performing force-directed layout.
 */
class StressTerm
{
public:
    StressTerm(double weight = 1);
    virtual double computeStress(unsigned n, std::valarray<double> X, std::valarray<double> Y,
                       unsigned short** G, double** D) = 0;
    virtual void computeForces(unsigned n, std::valarray<double> X, std::valarray<double> Y,
                       unsigned short** G, double** D,
                       vpsc::Dim dim, SparseMap &H, std::valarray<double> &g) = 0;
    double weight;
};

typedef std::vector<StressTerm*> StressTerms;

struct SoftDistribution
{
    SoftDistribution(vpsc::Dim dim)
    {
        this->dim = dim;
    }
    // Compute current separation for each pair.
    std::vector<double> seps(std::valarray<double> X, std::valarray<double> Y);
    void addIndexPair(unsigned u, unsigned v);
    std::vector<std::pair<unsigned, unsigned> > indexPairs;
    vpsc::Dim dim;
};

class SoftDistributionStressTerm : public StressTerm
{
public:
    SoftDistributionStressTerm(double weight = 1);
    void addSoftDistrubtion(SoftDistribution *sd);
    double computeStress(unsigned n, std::valarray<double> X, std::valarray<double> Y,
                   unsigned short** G, double** D);
    void computeForces(unsigned n, std::valarray<double> X, std::valarray<double> Y,
                   unsigned short** G, double** D,
                   vpsc::Dim dim, SparseMap &H, std::valarray<double> &g);
    std::vector<SoftDistribution*> sds;
};

} // namespace cola
#endif // _STRESS_TERMS_H
