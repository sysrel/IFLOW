
#include "WPA/WPAPass.h"
#include "MSSA/SVFG.h"

using namespace llvm;
using namespace std;

static llvm::cl::opt<std::string> InputFilename(cl::Positional,
        llvm::cl::desc("<input bitcode>"), llvm::cl::init("-"));


int main(int argc, char ** argv) {

    int arg_num = 0;
    char **arg_value = new char*[argc];
    std::vector<std::string> moduleNameVec;
    SVFUtil::processArguments(argc, argv, arg_num, arg_value, moduleNameVec);
    cl::ParseCommandLineOptions(arg_num, arg_value,
                                "Whole Program Points-to Analysis\n");

    SVFModule svfModule(moduleNameVec);

    WPAPass *wpa = new WPAPass();
    wpa->runOnModule(svfModule);

    //svfModule.dumpModulesToFile(".wpa");
    /*
    SVFG::SVFGNodeIDToNodeMapTy::iterator it = graph->begin();
    SVFG::SVFGNodeIDToNodeMapTy::iterator eit = graph->end();
    for (; it != eit; ++it)
    {
        SVFGNode* node = it->second; 
        const SVFGEdge::SVFGEdgeSetTy& inEdges = node->getInEdges();
        SVFGEdge::SVFGEdgeSetTy::const_iterator edgeIt = inEdges.begin();
        SVFGEdge::SVFGEdgeSetTy::const_iterator edgeEit = inEdges.end();
        for (; edgeIt != edgeEit; ++edgeIt)
        {
            const SVFGEdge *edge = *edgeIt;
            VFGNode *src = dyn_cast<VFGNode>(edge->getSrcNode());
            VFGNode *dest = dyn_cast<VFGNode>(edge->getDstNode());
            assert(src && dst);
            const ICFGNode *sicfg = src->getICFGNode();
            IntraBlockNode* sn = SVFUtil::dyn_cast<IntraBlockNode>(sicfg);
            const ICFGNode *dicfg = dest->getICFGNode();
            IntraBlockNode* dn = SVFUtil::dyn_cast<IntraBlockNode>(dicfg);
            if (sn && dn) {
               // source loc can be found for IntraBlockNode
               std::string ss = SVFUtil::getSourceLoc(sn->getInst());
               std::string ds = SVFUtil::getSourceLoc(dn->getInst());
               llvm::errs() << "[Intra] source: " << ss 
                            << " dest: " << ds << "\n";
            }
            else { // transitive closure  between program statements
               llvm::errs() << "[Other]" << edge->toString() << "\n"; 
            }
        }
    }
    */
    return 0;
}
