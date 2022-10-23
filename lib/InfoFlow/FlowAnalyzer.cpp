
#include "SABER/SrcSnkDDA.h"
#include "InfoFlow/FlowAnalyzer.h"
#include "MSSA/SVFGStat.h"

using namespace SVFUtil;

extern llvm::cl::opt<bool> DumpSlice;
extern llvm::cl::opt<unsigned> cxtLimit;


FlowAnalyzer::FlowAnalyzer() {
}

FlowAnalyzer::FlowAnalyzer(std::set<SVFGNode*> sources, std::set<SVFGNode*> sinks, SVFG *svfg) : CFLSrcSnkSolver()
 {
   for(auto s: sources) {
      const SVFGNode *cs = s;
      this->sources.insert(cs);
   }
   for(auto s: sinks) {
      const SVFGNode *cs = s;
      this->sinks.insert(cs);
   }
   /*sb = new SVFGBuilder(true);
   ander = AndersenWaveDiff::createAndersenWaveDiff(module);
   svfg = sb.buildPTROnlySVFGWithoutOPT(ander);*/
   this->svfg = svfg;
   setGraph(svfg);   
   pathCondAllocator = new PathCondAllocator();
   MemSSA *mssa = svfg->getMSSA();
   PointerAnalysis *pta = mssa->getPTA();
   SVFModule svfModule = pta->getModule();
   pathCondAllocator->allocate(svfModule.getMainLLVMModule());
}


bool FlowAnalyzer::isSource(const SVFGNode* node) {
   return (sources.find(node) != sources.end());
}

bool FlowAnalyzer::isSink(const SVFGNode* node) {
   return (sinks.find(node) != sinks.end());
}

void FlowAnalyzer::reportBug(ProgSlice* slice) {

    if(isAllPathReachable() == false && isSomePathReachable() == false) {
    }
    else if (isAllPathReachable() == false && isSomePathReachable() == true) {
        SVFUtil::errs() << "\t\t conditional path: \n" << slice->evalFinalCond() << "\n";
        //slice->annotatePaths();
    }

}


/*!
 * Recursively collect global memory objects
 */
void FlowAnalyzer::collectGlobals(BVDataPTAImpl* pta) {
    PAG* pag = svfg->getPAG();
    NodeVector worklist;
    for(PAG::iterator it = pag->begin(), eit = pag->end(); it!=eit; it++) {
        PAGNode* pagNode = it->second;
        if(SVFUtil::isa<DummyValPN>(pagNode) || SVFUtil::isa<DummyObjPN>(pagNode))
            continue;
        if(const Value* val = pagNode->getValue()) {
            if(SVFUtil::isa<GlobalVariable>(val))
                worklist.push_back(it->first);
        }
    }

    NodeToPTSSMap cachedPtsMap;
    while(!worklist.empty()) {
        NodeID id = worklist.back();
        worklist.pop_back();
        globs.set(id);
        PointsTo& pts = pta->getPts(id);
        for(PointsTo::iterator it = pts.begin(), eit = pts.end(); it!=eit; ++it) {
            globs |= CollectPtsChain(pta,*it,cachedPtsMap);
        }
    }
}

NodeBS& FlowAnalyzer::CollectPtsChain(BVDataPTAImpl* pta,NodeID id, NodeToPTSSMap& cachedPtsMap) {
    PAG* pag = svfg->getPAG();

    NodeID baseId = pag->getBaseObjNode(id);
    NodeToPTSSMap::iterator it = cachedPtsMap.find(baseId);
    if(it!=cachedPtsMap.end())
        return it->second;
    else {
        PointsTo& pts = cachedPtsMap[baseId];
        pts |= pag->getFieldsAfterCollapse(baseId);

        FIFOWorkList<NodeID> worklist;
        for(PointsTo::iterator it = pts.begin(), eit = pts.end(); it!=eit; ++it)
            worklist.push(*it);

        while(!worklist.empty()) {
            NodeID nodeId = worklist.pop();
            PointsTo& tmp = pta->getPts(nodeId);
            for(PointsTo::iterator it = tmp.begin(), eit = tmp.end(); it!=eit; ++it) {
               pts |= CollectPtsChain(pta,*it,cachedPtsMap);
            }
        }
        return pts;
    }
}

bool FlowAnalyzer::accessGlobal(BVDataPTAImpl* pta,const PAGNode* pagNode) {

    NodeID id = pagNode->getId();
    PointsTo pts = pta->getPts(id);
    pts.set(id);

    return pts.intersects(globs);
}

void FlowAnalyzer::rmDerefDirSVFGEdges(BVDataPTAImpl* pta) {

    for(SVFG::iterator it = svfg->begin(), eit = svfg->end(); it!=eit; ++it) {
        const SVFGNode* node = it->second;

        if(const StmtSVFGNode* stmtNode = SVFUtil::dyn_cast<StmtSVFGNode>(node)) {
            /// for store, connect the RHS/LHS pointer to its def
            if(SVFUtil::isa<StoreSVFGNode>(stmtNode)) {
                const SVFGNode* def = svfg->getDefSVFGNode(stmtNode->getPAGDstNode());
                SVFGEdge* edge = svfg->getSVFGEdge(def,stmtNode,SVFGEdge::IntraDirectVF);
                assert(edge && "Edge not found!");

                if(accessGlobal(pta,stmtNode->getPAGDstNode())) {
                    globSVFGNodes.insert(stmtNode);
                }
            }
            else if(SVFUtil::isa<LoadSVFGNode>(stmtNode)) {
                const SVFGNode* def = svfg->getDefSVFGNode(stmtNode->getPAGSrcNode());
                SVFGEdge* edge = svfg->getSVFGEdge(def,stmtNode,SVFGEdge::IntraDirectVF);
                assert(edge && "Edge not found!");
                //svfg->removeSVFGEdge(edge);

                if(accessGlobal(pta,stmtNode->getPAGSrcNode())) {
                    globSVFGNodes.insert(stmtNode);
                }
            }

        }
    }
}


void FlowAnalyzer::initialize() {

    MemSSA* mssa = svfg->getMSSA();
    BVDataPTAImpl* pta = mssa->getPTA();
    llvm::errs() << "\tCollect Global Variables\n";

    //collectGlobals(pta);   

    //rmDerefDirSVFGEdges(pta);
}

void FlowAnalyzer::analyze(std::set<std::pair<NodeID,NodeID> > &pairs, std::set<NodeID> &sanitizationSet) {
  
    sanitizers = &sanitizationSet;
    llvm::errs() << "Sanitizers:\n";
    for(auto s : sanitizationSet)
       llvm::errs() << "sanitizer node " << s << "\n";
    ContextCond::setMaxCxtLen(cxtLimit);

    for(auto source: sources) {

        forwardCandidates.clear();
        backwardEliminated.clear(); 

        setCurSlice(source);

        llvm::errs() << "Analysing slice:" << source->getId() << ")\n";
        ContextCond cxt;
        DPIm item(source->getId(),cxt);
        forwardTraverse(item);

        /// do not consider there is bug when reaching a global SVFGNode
        /// if we touch a global, then we assume the client uses this memory until the program exits.
        //if (getCurSlice()->isReachGlobal()) {
        //   llvm::errs() << "Forward analysis reaches globals for slice:" << source->getId() << "\n";
        //}
        //else {
            llvm::errs() << source->getId() << " (size = " << getCurSlice()->getForwardSliceSize() << ")\n"; 
            for (SVFGNodeSetIter sit = getCurSlice()->sinksBegin(), esit =
                        getCurSlice()->sinksEnd(); sit != esit; ++sit) {
                ContextCond cxt;
                DPIm item((*sit)->getId(),cxt);
                llvm::errs() << "backward traverse for " << (*sit)->getId() << "\n";
                backwardTraverse(item);
            }

            llvm::errs() << source->getId() << " (size = " << getCurSlice()->getBackwardSliceSize() << ")\n";

            AllPathReachability(pairs);

            llvm::errs() << "Guard computation for slice:" << source->getId() << ")\n";
        //}

        reportBug(getCurSlice());
    }

    finalize();
}


/*!
 * Propagate information forward by matching context
 */
void FlowAnalyzer::forwardpropagate(const DPIm& item, SVFGEdge* edge) {

    llvm::errs() << "Checking if a sanitization node " << edge->getSrcID() << "\n";
    if (sanitizers->find(edge->getSrcID()) != sanitizers->end()) {
       llvm::errs() << "Path terminated due to sanitization\n";
       return; 
    }

    llvm::errs() << "\n##processing source: " << getCurSlice()->getSource()->getId()
                        <<" forward propagate from (" << edge->getSrcID() << "\n";


    // for indirect SVFGEdge, the propagation should follow the def-use chains
    // points-to on the edge indicate whether the object of source node can be propagated

    const SVFGNode* dstNode = edge->getDstNode();
    DPIm newItem(dstNode->getId(),item.getContexts());

    /// perform context sensitive reachability
    // push context for calling
    if (edge->isCallVFGEdge()) {
        CallSiteID csId = 0;
        if(const CallDirSVFGEdge* callEdge = SVFUtil::dyn_cast<CallDirSVFGEdge>(edge))
            csId = callEdge->getCallSiteId();
        else
            csId = SVFUtil::cast<CallIndSVFGEdge>(edge)->getCallSiteId();

        newItem.pushContext(csId);
        llvm::errs() << " push cxt [" << csId << "] ";
    }
    // match context for return
    else if (edge->isRetVFGEdge()) {
        CallSiteID csId = 0;
        if(const RetDirSVFGEdge* callEdge = SVFUtil::dyn_cast<RetDirSVFGEdge>(edge))
            csId = callEdge->getCallSiteId();
        else
            csId = SVFUtil::cast<RetIndSVFGEdge>(edge)->getCallSiteId();

        if (newItem.matchContext(csId) == false) {
            llvm::errs() << "-|-\n";
            return;
        }
        llvm::errs() << " pop cxt [" << csId << "] ";
    }
    /// whether this dstNode has been visited or not
    if (forwardVisited(dstNode,newItem)) {
       llvm::errs() << " node "<< dstNode->getId() <<" has been visited\n";
       return;
    }
    else
        addForwardVisited(dstNode, newItem);

    if (pushIntoWorklist(newItem))
       llvm::errs() << " --> " << edge->getDstID() << ", cxt size: "
              << newItem.getContexts().cxtSize() <<")\n";
}


/*!
 * Propagate information backward without matching context, as forward analysis already did it
 */
void FlowAnalyzer::backwardpropagate(const DPIm& item, SVFGEdge* edge) {

    llvm::errs() << "backward propagate from (" << edge->getDstID() << " --> "
         << edge->getSrcID() << ")\n";
    const SVFGNode* srcNode = edge->getSrcNode();
    if(backwardVisited(srcNode))
        return;
    else
        addBackwardVisited(srcNode);

    ContextCond cxt;
    DPIm newItem(srcNode->getId(), cxt);
    pushIntoWorklist(newItem);
}


/// Guarded reachability search
void FlowAnalyzer::AllPathReachability(std::set<std::pair<NodeID,NodeID> > &pairs) {
    /// annotate SVFG with slice information for debugging purpose
    if(DumpSlice)
        annotateSlice(_curSlice);

    _curSlice->AllPathReachableSolve();

    if(isSatisfiableForAll(_curSlice)== true)
        _curSlice->setAllReachable();

    llvm::errs() << "is the slice reachable?\n";
    if (_curSlice->isPartialReachable() || _curSlice->isAllReachable()) {
       llvm::errs() << "yes, slice reachable, partial? " <<  _curSlice->isPartialReachable() 
                    << " all? " << _curSlice->isAllReachable() << "\n";
       const SVFGNodeSet& snks = _curSlice->getSinks();
       for(auto sink : snks) {
           pairs.insert(std::make_pair(_curSlice->getSource()->getId(), sink->getId()));
       }
    }
}

/// Set current slice
void FlowAnalyzer::setCurSlice(const SVFGNode* src) {
    if(_curSlice!=NULL) {
        //delete _curSlice;
        _curSlice = NULL;
        clearVisitedMap();
    }

    _curSlice = new ProgSlice(src,getPathAllocator(), getSVFG());
}


void FlowAnalyzer::annotateSlice(ProgSlice* slice) {
    getSVFG()->getStat()->addToSources(slice->getSource());
    for(SVFGNodeSetIter it = slice->sinksBegin(), eit = slice->sinksEnd(); it!=eit; ++it )
        getSVFG()->getStat()->addToSinks(*it);
    for(SVFGNodeSetIter it = slice->forwardSliceBegin(), eit = slice->forwardSliceEnd(); it!=eit; ++it )
        getSVFG()->getStat()->addToForwardSlice(*it);
    for(SVFGNodeSetIter it = slice->backwardSliceBegin(), eit = slice->backwardSliceEnd(); it!=eit; ++it)       
       getSVFG()->getStat()->addToBackwardSlice(*it);
}

void FlowAnalyzer::dumpSlices() {
    if(DumpSlice)
        const_cast<SVFG*>(getSVFG())->dump("Slice",true);
}

void FlowAnalyzer::printBDDStat() {

    llvm::errs() << "BDD Mem usage: " << PathCondAllocator::getMemUsage() << "\n";
    llvm::errs() << "BDD Number: " << PathCondAllocator::getCondNum() << "\n";
    llvm::errs() << "BDD max live number: " << PathCondAllocator::getMaxLiveCondNumber() << "\n";
}

