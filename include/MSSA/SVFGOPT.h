//===- SVFGOPT.h -- SVFG optimizer--------------------------------------------//
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
 * @file: SVFGOPT.h
 * @author: yesen
 * @date: 20/03/2014
 * @version: 1.0
 *
 * @section LICENSE
 *
 * @section DESCRIPTION
 *
 */


#ifndef SVFGOPT_H_
#define SVFGOPT_H_


#include "MSSA/SVFG.h"
#include "Util/WorkList.h"

#define INFOFLOW

/**
 * Optimised SVFG.
 * 1. FormalParam/ActualRet is converted into Phi. ActualParam/FormalRet becomes the
 *    operands of Phi nodes created at callee/caller's entry/callsite.
 * 2. ActualIns/ActualOuts resides at direct call sites id removed. Sources of its incoming
 *    edges are connected with the destinations of its outgoing edges directly.
 * 3. FormalIns/FormalOuts reside at the entry/exit of non-address-taken functions is
 *    removed as ActualIn/ActualOuts.
 * 4. MSSAPHI nodes are removed if it have no self cycle. Otherwise depends on user option.
 */
class SVFGOPT : public SVFG {
    typedef std::set<SVFGNode*> SVFGNodeSet;
    typedef std::map<NodeID, NodeID> NodeIDToNodeIDMap;
    typedef FIFOWorkList<const MSSAPHISVFGNode*> WorkList;

public:
    /// Constructor
    SVFGOPT(MemSSA* _mssa, VFGK kind) : SVFG(_mssa, kind) {
        keepAllSelfCycle = keepContextSelfCycle = keepActualOutFormalIn = false;
    }
    /// Destructor
    virtual ~SVFGOPT() {}

    inline void setTokeepActualOutFormalIn() {
        keepActualOutFormalIn = true;
    }
    inline void setTokeepAllSelfCycle() {
        keepAllSelfCycle = true;
    }
    inline void setTokeepContextSelfCycle() {
        keepContextSelfCycle = true;
    }

protected:
    virtual void buildSVFG();

    /// Connect SVFG nodes between caller and callee for indirect call sites
    //@{
    virtual inline void connectAParamAndFParam(const PAGNode* cs_arg, const PAGNode* fun_arg, CallSite cs, CallSiteID csId, SVFGEdgeSetTy& edges) {
        NodeID apId = getDef(cs_arg);
        NodeID phiId = getDef(fun_arg);
        //llvm::errs() << "Connecting aparam " << apId << " and fparam/phi " << phiId << " for callsite of function\n";
        //llvm::errs() << "phiId=" << phiId << "\n";
        SVFGNode *node = getSVFGNode(phiId);
        if (SVFUtil::isa<PHISVFGNode>(node)) {
           #ifdef INFOFLOW
           const SVFGNode* sn = getActualParmVFGNode(cs_arg,cs);
           SVFGEdge* edge = addCallEdge(sn->getId(), phiId, csId);
           //llvm::errs() << "adding call edge " << sn->getId() << "-->" << phiId << "\n";
           #else
           SVFGEdge* edge = addCallEdge(getDef(cs_arg), phiId, csId);
           //llvm::errs() << "adding call edge " << getDef(cs_arg) << "-->" << phiId << "\n";
           #endif
           if (edge != NULL) {
              PHISVFGNode* phi = SVFUtil::cast<PHISVFGNode>(getSVFGNode(phiId));
              addInterPHIOperands(phi, cs_arg);
              edges.insert(edge);
           }
        } 
        else if (SVFUtil::isa<FormalParmSVFGNode>(node)) { /* SYSREL EXTENSION */
             InterPHISVFGNode* phi = addInterPHIForFP(SVFUtil::dyn_cast<FormalParmSVFGNode>(node));
             //llvm::errs() << "phinode " << phi->getId() << "got created for " << node->getId() << " in aparam-fparam\n";
             //llvm::errs() << "Redirecting outgoing edges of of " << node->getId() << " from phi " << phi->getId() << "\n";
             /// migrate formal-param's edges to phi node.
            for (SVFGNode::const_iterator it = node->OutEdgeBegin(), eit = node->OutEdgeEnd();
                  it != eit; ++it) {
                const SVFGEdge* outEdge = *it;
                SVFGNode* dstNode = outEdge->getDstNode();
                NodeID dstId = dstNode->getId();   
                if (const CallDirSVFGEdge* callEdge = SVFUtil::dyn_cast<CallDirSVFGEdge>(outEdge)) {
                   addCallEdge(phi->getId(), dstId, callEdge->getCallSiteId());
                   //llvm::errs() << "Adding outgoing call edge " << phi->getId() << "-->" << dstId << "\n";
                }
                else if (const RetDirSVFGEdge* retEdge = SVFUtil::dyn_cast<RetDirSVFGEdge>(outEdge)) {
                  addRetEdge(phi->getId(), dstId, retEdge->getCallSiteId());
                  //llvm::errs() << "Adding outgoing ret edge " << phi->getId() << "-->" << dstId << "\n";
                }
                else {
                  addIntraDirectVFEdge(phi->getId(), dstId);
                  //llvm::errs() << "Adding intra edge " << phi->getId() << "-->" << dstId << "\n";
                }
             } 
             #ifdef INFOFLOW
             const SVFGNode* sn = getActualParmVFGNode(cs_arg,cs);
             SVFGEdge* edge = addCallEdge(sn->getId(), phi->getId(), csId);
             //llvm::errs() << "Adding incoming call edge " << sn->getId() << "-->" << phi->getId() << "\n";
             #else
             SVFGEdge* edge = addCallEdge(getDef(cs_arg), phi->getId(), csId);
             //llvm::errs() << "Adding incoming call edge " << getDef(cs_arg) << "-->" << phi->getId() << "\n";
             #endif
             if (edge != NULL) {
                addInterPHIOperands(phi, cs_arg);
                edges.insert(edge);
             }
        }
        else if (SVFUtil::isa<ActualParmSVFGNode>(node)) {
            assert(0 && "Actual par not handled!");
        }
    }
    /// Connect formal-ret and actual ret
    virtual inline void connectFRetAndARet(const PAGNode* fun_ret, const PAGNode* cs_ret, CallSiteID csId, SVFGEdgeSetTy& edges) {
        NodeID phiId = getDef(cs_ret);
        SVFGNode *node = getSVFGNode(phiId);
        if (SVFUtil::isa<PHISVFGNode>(node)) {
           #ifdef INFOFLOW
           const SVFGNode* sn = getFormalRetVFGNode(fun_ret);
           SVFGEdge* edge = addRetEdge(sn->getId(), phiId, csId);
           #else
           SVFGEdge* edge = addRetEdge(getDef(fun_ret), phiId, csId);
           #endif
           if (edge != NULL) {
              PHISVFGNode* phi = SVFUtil::cast<PHISVFGNode>(getSVFGNode(phiId));
              addInterPHIOperands(phi, fun_ret);
              edges.insert(edge);
           }
        }
        else if (SVFUtil::isa<ActualRetSVFGNode>(node)) {
           InterPHISVFGNode* phi = addInterPHIForAR(SVFUtil::dyn_cast<ActualRetSVFGNode>(node));
           for (SVFGNode::const_iterator it = node->OutEdgeBegin(), eit = node->OutEdgeEnd();
                  it != eit; ++it) {
                const SVFGEdge* outEdge = *it;
                SVFGNode* dstNode = outEdge->getDstNode();
                NodeID dstId = dstNode->getId();
                if (const CallDirSVFGEdge* callEdge = SVFUtil::dyn_cast<CallDirSVFGEdge>(outEdge))
                   addCallEdge(phi->getId(), dstId, callEdge->getCallSiteId());
                else if (const RetDirSVFGEdge* retEdge = SVFUtil::dyn_cast<RetDirSVFGEdge>(outEdge))
                  addRetEdge(phi->getId(), dstId, retEdge->getCallSiteId());
                else
                  addIntraDirectVFEdge(phi->getId(), dstId);
           }

           #ifdef INFOFLOW
           const SVFGNode* sn = getFormalRetVFGNode(fun_ret);
           SVFGEdge* edge = addRetEdge(sn->getId(), phi->getId(), csId);
           #else
           SVFGEdge* edge = addRetEdge(getDef(fun_ret), phi->getId(), csId);
           #endif
           if (edge != NULL) {
              addInterPHIOperands(phi, fun_ret);
              edges.insert(edge);
           }           
        }
    }
    /// Connect actual-in and formal-in
    virtual inline void connectAInAndFIn(const ActualINSVFGNode* actualIn, const FormalINSVFGNode* formalIn, CallSiteID csId, SVFGEdgeSetTy& edges) {
        PointsTo intersection = actualIn->getPointsTo();
        intersection &= formalIn->getPointsTo();
        if (intersection.empty() == false) {
           //llvm::errs() << "Actual in id=" << actualIn->getId() << "\n";
           // SYSREL: Since we choose to propagate the flows into indirect calls certain assumptions about defs may not hold
           if (actualInToDefMap.find(actualIn->getId()) == actualInToDefMap.end()) { // SYSREL EXTENSION
               SVFGNode::const_iterator it = actualIn->InEdgeBegin();
               SVFGNode::const_iterator eit = actualIn->InEdgeEnd();
               for (; it != eit; ++it) {
                   const IndirectSVFGEdge* inEdge = SVFUtil::cast<IndirectSVFGEdge>(*it);
                   SVFGNode *def = inEdge->getSrcNode();
                   setActualINDef(actualIn->getId(), def->getId());
                   break;
               }
            } // SYSREL EXTENSION
            NodeID aiDef = getActualINDef(actualIn->getId());
            //llvm::errs() << "Connecting actual in " << actualIn->getId() << " with formal in " << formalIn->getId() << "\n";
            #ifdef INFOFLOW
            SVFGEdge* edge = addCallIndirectSVFGEdge(actualIn->getId(),formalIn->getId(),csId,intersection);
            #else
            SVFGEdge* edge = addCallIndirectSVFGEdge(aiDef,formalIn->getId(),csId,intersection);
            #endif
            if (edge != NULL) 
                edges.insert(edge);
        }
    }
    /// Connect formal-out and actual-out
    virtual inline void connectFOutAndAOut(const FormalOUTSVFGNode* formalOut, const ActualOUTSVFGNode* actualOut, CallSiteID csId, SVFGEdgeSetTy& edges) {
        PointsTo intersection = formalOut->getPointsTo();
        intersection &= actualOut->getPointsTo();
        if (intersection.empty() == false) {
           // SYSREL: Since we choose to propagate the flows into indirect calls certain assumptions about defs may not hold
           if (formalOutToDefMap.find(formalOut->getId()) == formalOutToDefMap.end()) { // SYSREL EXTENSION
               SVFGNode::const_iterator it = formalOut->InEdgeBegin();
               SVFGNode::const_iterator eit = formalOut->InEdgeEnd();
               for (; it != eit; ++it) {
                   const IndirectSVFGEdge* inEdge = SVFUtil::cast<IndirectSVFGEdge>(*it);
                   SVFGNode *def = inEdge->getSrcNode();
                   setFormalOUTDef(formalOut->getId(), def->getId());              
                   break;
               } 
           } // SYSREL EXTENSION
           NodeID foDef = getFormalOUTDef(formalOut->getId());
           #ifdef INFOFLOW  
           //llvm::errs() << "Connecting formal out " << formalOut->getId() << " with actual out " << actualOut->getId() << "\n";
           SVFGEdge* edge = addRetIndirectSVFGEdge(formalOut->getId(),actualOut->getId(),csId,intersection);
           #else
           SVFGEdge* edge = addRetIndirectSVFGEdge(foDef,actualOut->getId(),csId,intersection);
           #endif 
           if (edge != NULL)
                edges.insert(edge);
        }
    }
    //@}

    /// Get def-site of actual-in/formal-out.
    //@{
    inline NodeID getActualINDef(NodeID ai) const {
        NodeIDToNodeIDMap::const_iterator it = actualInToDefMap.find(ai);
        assert(it != actualInToDefMap.end() && "can not find actual-in's def");
        return it->second;
    }
    inline NodeID getFormalOUTDef(NodeID fo) const {
        NodeIDToNodeIDMap::const_iterator it = formalOutToDefMap.find(fo);
        assert(it != formalOutToDefMap.end() && "can not find formal-out's def");
        return it->second;
    }
    //@}

private:
    void parseSelfCycleHandleOption();

    /// Add inter-procedural value flow edge
    //@{
    /// Add indirect call edge from src to dst with one call site ID.
    SVFGEdge* addCallIndirectSVFGEdge(NodeID srcId, NodeID dstId, CallSiteID csid, const PointsTo& cpts);
    /// Add indirect ret edge from src to dst with one call site ID.
    SVFGEdge* addRetIndirectSVFGEdge(NodeID srcId, NodeID dstId, CallSiteID csid, const PointsTo& cpts);
    //@}

    /// 1. Convert FormalParmSVFGNode into PHISVFGNode and add all ActualParmSVFGNoe which may
    /// propagate pts to it as phi's operands.
    /// 2. Do the same thing for ActualRetSVFGNode and FormalRetSVFGNode.
    /// 3. Record def site of ActualINSVFGNode. Remove all its edges and connect its predecessors
    ///    and successors.
    /// 4. Do the same thing for FormalOUTSVFGNode as 3.
    /// 5. Remove ActualINSVFGNode/FormalINSVFGNode/ActualOUTSVFGNode/FormalOUTSVFGNode if they
    ///    will not be used when updating call graph.
    void handleInterValueFlow();

    /// Replace FormalParam/ActualRet node with PHI node.
    //@{
    void replaceFParamARetWithPHI(PHISVFGNode* phi, SVFGNode* svfgNode);
    //@}

    /// Retarget edges related to actual-in/-out and formal-in/-out.
    //@{
    /// Record def sites of actual-in/formal-out and connect from those def-sites
    /// to formal-in/actual-out directly if they exist.
    void retargetEdgesOfAInFOut(SVFGNode* node);
    /// Connect actual-out/formal-in's predecessors to their successors directly.
    void retargetEdgesOfAOutFIn(SVFGNode* node);
    //@}

    /// Remove MSSAPHI SVFG nodes.
    void handleIntraValueFlow();

    /// Initial work list with MSSAPHI nodes which may be removed.
    inline void initialWorkList() {
        for (SVFG::const_iterator it = begin(), eit = end(); it != eit; ++it)
            addIntoWorklist(it->second);
    }

    /// Only MSSAPHI node which satisfy following conditions will be removed:
    /// 1. it's not def-site of actual-in/formal-out;
    /// 2. it doesn't have incoming and outgoing call/ret at the same time.
    inline bool addIntoWorklist(const SVFGNode* node) {
        if (const MSSAPHISVFGNode* phi = SVFUtil::dyn_cast<MSSAPHISVFGNode>(node)) {
            if (isConnectingTwoCallSites(phi) == false && isDefOfAInFOut(phi) == false)
                return worklist.push(phi);
        }
        return false;
    }

    /// Remove MSSAPHI node if possible
    void bypassMSSAPHINode(const MSSAPHISVFGNode* node);

    /// Remove self cycle edges if needed. Return TRUE if some self cycle edges remained.
    bool checkSelfCycleEdges(const MSSAPHISVFGNode* node);

    /// Add new SVFG edge from src to dst.
    bool addNewSVFGEdge(NodeID srcId, NodeID dstId, const SVFGEdge* preEdge, const SVFGEdge* succEdge);

    /// Return TRUE if both edges are indirect call/ret edges.
    inline bool bothInterEdges(const SVFGEdge* edge1, const SVFGEdge* edge2) const {
        bool inter1 = (SVFUtil::isa<CallIndSVFGEdge>(edge1) || SVFUtil::isa<RetIndSVFGEdge>(edge1));
        bool inter2 = (SVFUtil::isa<CallIndSVFGEdge>(edge2) || SVFUtil::isa<RetIndSVFGEdge>(edge2));
        return (inter1 && inter2);
    }

    inline void addInterPHIOperands(PHISVFGNode* phi, const PAGNode* operand) {
        phi->setOpVer(phi->getOpVerNum(), operand);
    }

    /// Add inter PHI SVFG node for formal parameter
    inline InterPHISVFGNode* addInterPHIForFP(const FormalParmSVFGNode* fp) {
        InterPHISVFGNode* sNode = new InterPHISVFGNode(totalVFGNode++,fp);
        addSVFGNode(sNode);
        resetDef(fp->getParam(),sNode);
        return sNode;
    }
    /// Add inter PHI SVFG node for actual return
    inline InterPHISVFGNode* addInterPHIForAR(const ActualRetSVFGNode* ar) {
        InterPHISVFGNode* sNode = new InterPHISVFGNode(totalVFGNode++,ar);
        addSVFGNode(sNode);
        resetDef(ar->getRev(),sNode);
        return sNode;
    }

    inline void resetDef(const PAGNode* pagNode, const SVFGNode* node) {
        PAGNodeToDefMapTy::iterator it = PAGNodeToDefMap.find(pagNode);
        assert(it != PAGNodeToDefMap.end() && "a PAG node doesn't have definition before");
        PAGNodeToDefMap[pagNode] = node->getId();
    }

    /// Set def-site of actual-in/formal-out.
    ///@{
    inline void setActualINDef(NodeID ai, NodeID def) {
        NodeIDToNodeIDMap::const_iterator it = actualInToDefMap.find(ai);
        assert(it == actualInToDefMap.end() && "can not set actual-in's def twice");
        actualInToDefMap[ai] = def;
        defNodes.set(def);
    }
    inline void setFormalOUTDef(NodeID fo, NodeID def) {
        NodeIDToNodeIDMap::const_iterator it = formalOutToDefMap.find(fo);
        assert(it == formalOutToDefMap.end() && "can not set formal-out's def twice");
        formalOutToDefMap[fo] = def;
        defNodes.set(def);
    }
    ///@}

    inline bool isDefOfAInFOut(const SVFGNode* node) {
        return defNodes.test(node->getId());
    }

    /// Check if actual-in/actual-out exist at indirect call site.
    //@{
    inline bool actualInOfIndCS(const ActualINSVFGNode* ai) const {
        return (PAG::getPAG()->isIndirectCallSites(ai->getCallSite()));
    }
    inline bool actualOutOfIndCS(const ActualOUTSVFGNode* ao) const {
        return (PAG::getPAG()->isIndirectCallSites(ao->getCallSite()));
    }
    //@}

    /// Check if formal-in/formal-out reside in address-taken function.
    //@{
    inline bool formalInOfAddressTakenFunc(const FormalINSVFGNode* fi) const {
        return (fi->getEntryChi()->getFunction()->hasAddressTaken());
    }
    inline bool formalOutOfAddressTakenFunc(const FormalOUTSVFGNode* fo) const {
        return (fo->getRetMU()->getFunction()->hasAddressTaken());
    }
    //@}

    /// Return TRUE if this node has both incoming call/ret and outgoing call/ret edges.
    bool isConnectingTwoCallSites(const SVFGNode* node) const;

    /// Return TRUE if this SVFGNode can be removed.
    /// Nodes can be removed if it is:
    /// 1. ActualParam/FormalParam/ActualRet/FormalRet
    /// 2. ActualIN if it doesn't reside at indirect call site
    /// 3. FormalIN if it doesn't reside at the entry of address-taken function and it's not
    ///    definition site of ActualIN
    /// 4. ActualOUT if it doesn't reside at indirect call site and it's not definition site
    ///    of FormalOUT
    /// 5. FormalOUT if it doesn't reside at the exit of address-taken function
    bool canBeRemoved(const SVFGNode * node);

    /// Remove edges of a SVFG node
    //@{
    inline void removeAllEdges(const SVFGNode* node) {
        removeInEdges(node);
        removeOutEdges(node);
    }
    inline void removeInEdges(const SVFGNode* node) {
        /// remove incoming edges
        while (node->hasIncomingEdge())
            removeSVFGEdge(*(node->InEdgeBegin()));
    }
    inline void removeOutEdges(const SVFGNode* node) {
        while (node->hasOutgoingEdge())
            removeSVFGEdge(*(node->OutEdgeBegin()));
    }
    //@}


    NodeIDToNodeIDMap actualInToDefMap;	///< map actual-in to its def-site node
    NodeIDToNodeIDMap formalOutToDefMap;	///< map formal-out to its def-site node
    NodeBS defNodes;	///< preserved def nodes of formal-in/actual-out

    WorkList worklist;	///< storing MSSAPHI nodes which may be removed.

    bool keepActualOutFormalIn;
    bool keepAllSelfCycle;
    bool keepContextSelfCycle;
};


#endif /* SVFGOPT_H_ */
