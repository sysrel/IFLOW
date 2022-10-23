//===- ConsttimeChecker.h -- Detecting consttime issues--------------------------------//
//
//                     SVF: Static Value-Flow Analysis
//
// Copyright (C) <2013-2017>  <Yulei Sui>
//

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//===----------------------------------------------------------------------===//

/*
 * ConsttimeChecker.h
 *
 *  Created on: May 23rd, 2020
 *      Author: Tuba Yavuz
 */

#ifndef CONSTTIMECHECKER_H_
#define CONSTTIMECHECKER_H_

#include "SABER/SrcSnkDDA.h"
#include "SABER/SaberCheckerAPI.h"

/*!
 * Static Consttime Issue Detector
 */
class ConsttimeChecker : public SrcSnkDDA, public ModulePass {

public:
    typedef std::map<const SVFGNode*,CallSite> SVFGNodeToCSIDMap;
    typedef FIFOWorkList<CallSite> CSWorkList;
    typedef ProgSlice::VFWorkList WorkList;
    typedef NodeBS SVFGNodeBS;
    typedef PAG::CallSiteSet CallSiteSet;

    /// Pass ID
    static char ID;

    /// Constructor
    ConsstimeChecker(char id = ID): ModulePass(ID) {
    }
    /// Destructor
    virtual ~ConsttimeChecker() {
    }
    /// We start from here
    virtual bool runOnModule(llvm::Module& module) {
        SVFModule svfModule(module);
        return runOnModule(svfModule);
    }

    /// We start from here
    virtual bool runOnModule(SVFModule module) {
        /// start analysis
        analyze(module);
        return false;
    }

    /// Get pass name
    virtual StringRef getPassName() const {
        return "Static Consttime Flow Analysis";
    }

    /// Pass dependence
    virtual void getAnalysisUsage(AnalysisUsage& au) const {
        /// do not intend to change the IR in this pass,
        au.setPreservesAll();
    }

    /// Initialize sources and sinks
    //@{
    /// Initialize sources and sinks
    virtual void initSrcs();
    virtual void initSnks();
    /// Whether the function is a consttime setter
    virtual inline bool isSourceLikeFun(const Function* fun) {
        return SaberCheckerAPI::getCheckerAPI()->isConsttimeSetter(fun);
    }
    /// Whether the function is a consttime sensitive one
    virtual inline bool isSinkLikeFun(const Function* fun) {
        return SaberCheckerAPI::getCheckerAPI()->isConsttimeSensitive(fun);
    }
    /// ???
    bool isInAWrapper(const SVFGNode* src, CallSiteSet& csIdSet);
    inline bool isSource(const SVFGNode* node) {
        return getSources().find(node)!=getSources().end();
    }
    inline bool isSink(const SVFGNode* node) {
        return getSinks().find(node)!=getSinks().end();
    }
    //@}

protected:
    /// Get PAG
    PAG* getPAG() const {
        return PAG::getPAG();
    }
    /// Report leaks
    //@{
    virtual void reportBug(ProgSlice* slice);
    void reportNeverConsttime(const SVFGNode* src);
    void reportPartialConsttime(const SVFGNode* src);
    //@}

    /// Validate test cases for regression test purpose
    void testsValidation(const ProgSlice* slice);
    void validateSuccessTests(const SVFGNode* source, const Function* fun);
    void validateExpectedFailureTests(const SVFGNode* source, const Function* fun);

    /// Record a source to its callsite
    //@{
    inline void addSrcToCSID(const SVFGNode* src, CallSite cs) {
        srcToCSIDMap[src] = cs;
    }
    inline CallSite getSrcCSID(const SVFGNode* src) {
        SVFGNodeToCSIDMap::iterator it =srcToCSIDMap.find(src);
        assert(it!=srcToCSIDMap.end() && "source node not at a callsite??");
        return it->second;
    }
    //@}
private:
    SVFGNodeToCSIDMap srcToCSIDMap;
};

#endif /* LEAKCHECKER_H_ */
