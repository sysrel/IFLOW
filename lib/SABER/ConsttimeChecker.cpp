//===- ConsttimeChecker.cpp -- Consttime issue detector ------------------------------//
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
 * ConsttimeChecker.cpp
 *
 *  Created on: May 24th, 2020
 *      Author: Tuba Yavuz
 */

#include "SABER/ConsttimeChecker.h"
#include "Util/SVFUtil.h"

using namespace SVFUtil;

char ConsttimeChecker::ID = 0;

static llvm::RegisterPass<ConsttimeChecker> CONSTTIMECHECKER("consttime-checker",
        "Consttime Issue Checker");
static llvm::cl::opt<bool> ValidateTests("valid-tests", llvm::cl::init(false),
                                   llvm::cl::desc("Validate consttime tests"));

/*!
 * Initialize sources
 */
void ConsttimeChecker::initSrcs() {

    PAG* pag = getPAG();
    for(PAG::CSToArgsListMap::iterator it = pag->getCallSiteArgsMap().begin(),
            eit = pag->getCallSiteArgsMap().end(); it != eit; it++) {
        CallSite cs = it->first;
        /// if this callsite return reside in a dead function then we do not care about it
        if(isPtrInDeadFunction(cs.getInstruction()))
            continue;

        const Function* fun = getCallee(cs);
        if (isSourceLikeFun(fun)) {
           PAG::PAGNodeList& arglist = it->second;
           assert(sourceToArgsMap.find(fun->getName()) != sourceToArgsMap.end() && 
                 "source args not recorded\n");
           std::set<unisgned> args = sourceToArgsMap[fun->getName()];
           unsigned i=0;
           for(auto arg : args) {
              if (args.find(i) != args.end()) {
                 const SVFGNode* src = getSVFG()->getActualParmVFGNode(*arg,it->first);
                 addToSinks(src);
              }
              i++;
           } 
        }
    }
}

/*!
 * Initialize sinks
 */
void ConsttimeChecker::initSnks() {

    PAG* pag = getPAG();

    for(PAG::CSToArgsListMap::iterator it = pag->getCallSiteArgsMap().begin(),
            eit = pag->getCallSiteArgsMap().end(); it!=eit; ++it) {
        const Function* fun = getCallee(it->first);
        if(isSinkLikeFun(fun)) {
            PAG::PAGNodeList& arglist =	it->second;
            assert(!arglist.empty() && "no actual parameter at consttime sensitive site?");
            // read the consttime sensitive parameters of the sink 
            // using the map initialized in the constructor
            assert(sinkToArgsMap.find(fun->getName()) != sinkToArgsMap.end() && 
                   "Consttime sensitive function nore recorded in the args map!\n");
            std::set<unsigned> pars = sinkToArgsMap[fun->getName()];
            unsigned int i=0;
            for(auto arg : arglist) {
               if (pars.find(i) != pars.end()) {
                  const SVFGNode* snk = getSVFG()->getActualParmVFGNode(*arg,it->first);
                  addToSinks(snk);
               }
               i++;
            }
        }
    }
}


void ConsttimeChecker::reportNever(const SVFGNode* src) {
    CallSite cs = getSrcCSID(src);
    SVFUtil::errs() << bugMsg1("\t Never :") <<  " consistent bignum at : ("
           << getSourceLoc(cs.getInstruction()) << ")\n";
}

void ConsttimeChecker::reportPartial(const SVFGNode* src) {
    CallSite cs = getSrcCSID(src);
    SVFUtil::errs() << bugMsg2("\t Partial :") <<  " bignum not consistent : ("
           << getSourceLoc(cs.getInstruction()) << ")\n";
}

void ConsttimeChecker::reportBug(ProgSlice* slice) {

    if(isAllPathReachable() == false && isSomePathReachable() == false) {
        reportNever(slice->getSource());
    }
    else if (isAllPathReachable() == false && isSomePathReachable() == true) {
        reportPartial(slice->getSource());
        SVFUtil::errs() << "\t\t conditional consistent path: \n" << slice->evalFinalCond() << "\n";
        slice->annotatePaths();
    }

    if(ValidateTests)
        testsValidation(slice);
}


/*!
 * Validate test cases for regression test purpose
 */
void ConsttimeChecker::testsValidation(const ProgSlice* slice) {
    const SVFGNode* source = slice->getSource();
    CallSite cs = getSrcCSID(source);
    const Function* fun = getCallee(cs);
    if(fun==NULL)
        return;

    validateSuccessTests(source,fun);
    validateExpectedFailureTests(source,fun);
}


void ConsttimeChecker::validateSuccessTests(const SVFGNode* source, const Function* fun) {

    CallSite cs = getSrcCSID(source);

    bool success = false;

    if(fun->getName() == "SAFEMALLOC") {
        if(isAllPathReachable() == true && isSomePathReachable() == true)
            success = true;
    }
    else if(fun->getName() == "NFRMALLOC") {
        if(isAllPathReachable() == false && isSomePathReachable() == false)
            success = true;
    }
    else if(fun->getName() == "PLKMALLOC") {
        if(isAllPathReachable() == false && isSomePathReachable() == true)
            success = true;
    }
    else if(fun->getName() == "CLKMALLOC") {
        if(isAllPathReachable() == false && isSomePathReachable() == false)
            success = true;
    }
    else if(fun->getName() == "NFRLEAKFP" || fun->getName() == "PLKLEAKFP"
            || fun->getName() == "LEAKFN") {
        return;
    }
    else {
        wrnMsg("\t can not validate, check function not found, please put it at the right place!!");
        return;
    }

    std::string funName = source->getBB()->getParent()->getName();

    if (success)
        outs() << sucMsg("\t SUCCESS :") << funName << " check <src id:" << source->getId()
               << ", cs id:" << *getSrcCSID(source).getInstruction() << "> at ("
               << getSourceLoc(cs.getInstruction()) << ")\n";
    else
    	SVFUtil::errs() << errMsg("\t FAILURE :") << funName << " check <src id:" << source->getId()
               << ", cs id:" << *getSrcCSID(source).getInstruction() << "> at ("
               << getSourceLoc(cs.getInstruction()) << ")\n";
}

void ConstiitmeChecker::validateExpectedFailureTests(const SVFGNode* source, const Function* fun) {

    CallSite cs = getSrcCSID(source);

    bool expectedFailure = false;

    if(fun->getName() == "NFRLEAKFP") {
        if(isAllPathReachable() == false && isSomePathReachable() == false)
            expectedFailure = true;
    }
    else if(fun->getName() == "PLKLEAKFP") {
        if(isAllPathReachable() == false && isSomePathReachable() == true)
            expectedFailure = true;
    }
    else if(fun->getName() == "LEAKFN") {
        if(isAllPathReachable() == true && isSomePathReachable() == true)
            expectedFailure = true;
    }
    else if(fun->getName() == "SAFEMALLOC" || fun->getName() == "NFRMALLOC"
            || fun->getName() == "PLKMALLOC" || fun->getName() == "CLKLEAKFN") {
        return;
    }
    else {
        wrnMsg("\t can not validate, check function not found, please put it at the right place!!");
        return;
    }

    std::string funName = source->getBB()->getParent()->getName();

    if (expectedFailure)
        outs() << sucMsg("\t EXPECTED FAIL :") << funName << " check <src id:" << source->getId()
               << ", cs id:" << *getSrcCSID(source).getInstruction() << "> at ("
               << getSourceLoc(cs.getInstruction()) << ")\n";
    else
    	SVFUtil::errs() << errMsg("\t UNEXPECTED FAIL :") << funName << " check <src id:" << source->getId()
               << ", cs id:" << *getSrcCSID(source).getInstruction() << "> at ("
               << getSourceLoc(cs.getInstruction()) << ")\n";
}
