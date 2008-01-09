/**
 *
 * Authors:
 *   Tim Dwyer <tgdwyer@gmail.com>
 *
 * Copyright (C) 2005 Authors
 *
 * Released under GNU LGPL.  Read the file 'COPYING' for more information.
 */
#ifndef SEEN_REMOVEOVERLAP_VARIABLE_H
#define SEEN_REMOVEOVERLAP_VARIABLE_H

#include <vector>
#include <iostream>
#include "block.h"

namespace vpsc {

class Constraint;
typedef std::vector<Constraint*> Constraints;
class Variable
{
	friend std::ostream& operator <<(std::ostream &os, const Variable &v);
	friend class Block;
	friend class Constraint;
	friend class Solver;
public:
	int id; // useful in log files
	double desiredPosition;
	double finalPosition;
	double weight; // how much the variable wants to 
	               // be at it's desired position
	double scale; // translates variable to another space
	double offset;
	Block *block;
	bool visited;
	bool fixedDesiredPosition;
	Constraints in;
	Constraints out;
	char *toString();
	inline Variable(const int id, const double desiredPos=-1.0, 
			const double weight=1.0, const double scale=1.0)
		: id(id)
		, desiredPosition(desiredPos)
		, weight(weight)
		, scale(scale)
		, offset(0)
		, block(NULL)
		, visited(false)
		, fixedDesiredPosition(false)
	{
	}
	double dfdv() const {
		return 2. * weight * ( position() - desiredPosition );
	}
	//double position() const;
	~Variable(void) {
		in.clear();
		out.clear();
	}
private:
	double position() const {
		return (block->ps.scale*block->posn+offset)/scale;
	}
};
typedef std::vector<Variable*> Variables;
}
#endif // SEEN_REMOVEOVERLAP_VARIABLE_H
