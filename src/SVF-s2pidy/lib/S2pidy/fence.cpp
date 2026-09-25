#include "S2pidy/util.h"
#include <thread>

using namespace llvm;
using namespace std;
using namespace SVF;

#include <chrono>


#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Dominators.h"

void traverseBackwardsFlowSensitiveForRust(
    ICFG* icfg,
    const ICFGNode* iNode,
    PTACallGraph* callgraph,
    std::set<std::string>& allowlist) {

    // ICFGNode* iNode = icfg->getBlockICFGNode(inst);
    const SVFFunction* targetFun = iNode->getFun();

    FIFOWorkList<const ICFGNode*> worklist;
    std::set<ICFGEdge*> visited;
    worklist.push(iNode);


    /// Traverse along ICFG
    while(!worklist.empty()) 
    {
        const ICFGNode* vNode = worklist.pop();
        const SVFFunction* vNodeFun = vNode->getFun();
        if(!vNodeFun) continue;
        const Function* vFun = vNodeFun->getLLVMFun();

        // Check if the node has calls to library functions, if so add them to allowlist
        // This ends up being necessary in cases like CGC where certain functions were compiled with
        // our instrumentation but end up being used as shared libraries. Such functions will not 
        // be observed during our static analysis but will only have a stub call. Therefore, they need
        // to be identified and added to our allowlist
        
        if (SVFUtil::isa<CallBlockNode>(vNode)) {
            for (ICFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = 
                    vNode->OutEdgeEnd(); it != eit; ++it) {
                ICFGEdge* libEdge = *it;
                if (CallCFGEdge* dirCall = SVFUtil::dyn_cast<CallCFGEdge>(libEdge)) {
                    ICFGNode* libNode = libEdge->getDstNode();
                    // Validate that the dest Node has no outgoing edge
                    if(!libNode->hasOutgoingEdge()) {
                        const SVFFunction* prevFun = libNode->getFun();
                        if(!prevFun) continue;
                        const Function* candidate = prevFun->getLLVMFun();
                        allowlist.insert(candidate->getName().str());
                    }
                }
            }
        }

        for(ICFGNode::const_iterator it = vNode->InEdgeBegin(), eit =
                vNode->InEdgeEnd(); it != eit; ++it) {
            ICFGEdge* edge = *it;
            ICFGNode* prevNode = edge->getSrcNode();

            const SVFFunction* prevFun = prevNode->getFun();
            if(!prevNode) continue;
            const Function* candidate = prevFun->getLLVMFun();
            //if not visited yet
            if (visited.find(edge) == visited.end()) {
                //if prev_node is Call Node, we need to check whether the previous icfg node is actually reachable to
                //target node (context-sensitive) through call graph reachability analysis
                if(CallCFGEdge* dirCall = SVFUtil::dyn_cast<CallCFGEdge>(edge)) {
                    if(isReachableForRust(prevFun, targetFun, callgraph)) {
                        worklist.push(prevNode);
                        allowlist.insert(candidate->getName().str());
                    }
                }
                // if prev_node is not Call Node, just add this node to allowlist and worklist
                else {
                    worklist.push(prevNode);
                    allowlist.insert(candidate->getName().str());
                }
                visited.insert(edge);
            }

        }

    }
}


void traverseBackwardsFlowSensitiveMode1(
    ICFG* icfg,
    const ICFGNode* iNode,
    PTACallGraph* callgraph,
    std::set<std::string>& allowlist,
    std::set<const Function*>& reachableFuncs
    ) {

    // ICFGNode* iNode = icfg->getBlockICFGNode(inst);
    const SVFFunction* targetFun = iNode->getFun();

    FIFOWorkList<const ICFGNode*> worklist;
    std::set<ICFGEdge*> visited;
    worklist.push(iNode);


    /// Traverse along ICFG
    while(!worklist.empty()) 
    {
        const ICFGNode* vNode = worklist.pop();
        const SVFFunction* vNodeFun = vNode->getFun();
        if(!vNodeFun) continue;
        const Function* vFun = vNodeFun->getLLVMFun();

        // Check if the node has calls to library functions, if so add them to allowlist
        // This ends up being necessary in cases like CGC where certain functions were compiled with
        // our instrumentation but end up being used as shared libraries. Such functions will not 
        // be observed during our static analysis but will only have a stub call. Therefore, they need
        // to be identified and added to our allowlist
        
        if (SVFUtil::isa<CallBlockNode>(vNode)) {
            for (ICFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = 
                    vNode->OutEdgeEnd(); it != eit; ++it) {
                ICFGEdge* libEdge = *it;
                if (CallCFGEdge* dirCall = SVFUtil::dyn_cast<CallCFGEdge>(libEdge)) {
                    ICFGNode* libNode = libEdge->getDstNode();
                    // Validate that the dest Node has no outgoing edge
                    if(!libNode->hasOutgoingEdge()) {
                        const SVFFunction* prevFun = libNode->getFun();
                        if(!prevFun) continue;
                        const Function* candidate = prevFun->getLLVMFun();
                        allowlist.insert(candidate->getName().str());
                        reachableFuncs.insert(candidate);
                    }
                }
            }
        }

        for(ICFGNode::const_iterator it = vNode->InEdgeBegin(), eit =
                vNode->InEdgeEnd(); it != eit; ++it) {
            ICFGEdge* edge = *it;
            ICFGNode* prevNode = edge->getSrcNode();

            const SVFFunction* prevFun = prevNode->getFun();
            if(!prevNode) continue;
            const Function* candidate = prevFun->getLLVMFun();
            //if not visited yet
            if (visited.find(edge) == visited.end()) {
                //if prev_node is Call Node, we need to check whether the previous icfg node is actually reachable to
                //target node (context-sensitive) through call graph reachability analysis
                if(CallCFGEdge* dirCall = SVFUtil::dyn_cast<CallCFGEdge>(edge)) {
                    if(isReachableForRust(prevFun, targetFun, callgraph)) {
                        worklist.push(prevNode);
                        allowlist.insert(candidate->getName().str());
                        reachableFuncs.insert(candidate);
                    }
                }
                // if prev_node is not Call Node, just add this node to allowlist and worklist
                else {
                    worklist.push(prevNode);
                    allowlist.insert(candidate->getName().str());
                    reachableFuncs.insert(candidate);
                }
                visited.insert(edge);
            }

        }

    }
}




std::set<const BasicBlock*> countAllBB(SVFModule* svfModule, ICFG* icfg, int* allbb ,size_t fuzzing_options) {
    const Instruction* main_inst = get_main_inst(svfModule, fuzzing_options);
    // const Instruction* main_inst = get_target_inst("main", svfModule);
    ICFGNode* mainNode = icfg->getBlockICFGNode(main_inst);
    FIFOWorkList<const ICFGNode*> worklist;
    std::set<ICFGEdge*> visited;
    std::set<const Function*> all_funcs;
    std::set<const BasicBlock*> all_BB;

    std::set<const Function*> std_funcs;
    std::set<const BasicBlock*> std_BB;
    worklist.push(mainNode);

    // ofstream myfile ("countAllBB.txt");

    // if(!myfile.is_open()) {
    //     std::cout << "could not open output file exiting";
    //     exit(1);
    // }

    /// Traverse along ICFG
    while(!worklist.empty())
    {
        const ICFGNode* vNode = worklist.pop();
        for(ICFGNode::const_iterator it = vNode->OutEdgeBegin(), eit =
                vNode->OutEdgeEnd(); it != eit; ++it) {
            ICFGEdge* edge = *it;
            ICFGNode* nextNode = edge->getDstNode();
            const SVFFunction* nextFun = nextNode->getFun();
            if (visited.find(edge) == visited.end()) {

                // Std library BB blocks
                if(nextFun && (isBlacklisted(nextFun->getLLVMFun()) || isDroplisted(nextFun->getLLVMFun()))) {   
                    if(std_funcs.find(nextFun->getLLVMFun()) == std_funcs.end()) {
                        std_funcs.insert(nextFun->getLLVMFun());
                    }

                    const BasicBlock* stdbb = nextNode->getBB();
                    if(stdbb != nullptr) {
                        if(std_BB.find(stdbb) == std_BB.end()) {
                            std_BB.insert(stdbb);
                        }
                    }
                }
                
                // All BB blocks
                if(all_funcs.find(nextFun->getLLVMFun()) == all_funcs.end()) {
                    all_funcs.insert(nextFun->getLLVMFun());
                }
                const BasicBlock* bb = nextNode->getBB();
                if(bb != nullptr) {
                    if(all_BB.find(bb) == all_BB.end()) {
                        all_BB.insert(bb);
                    }
                }

                worklist.push(nextNode);
                visited.insert(edge);

            }
        }
    }


    int j = 0;
    int i = 0;
    for(const auto & bb : all_BB) {
        i++;
    }

    for(const auto & bb : std_BB) {
        j++;
    }

    // myfile << "ALL BB count : [" << i << "]" << "\n";
    // myfile << "STD BB count : [" << j << "]" << "\n";
    // myfile << "Only Source Code BB count : [" << i-j << "]" << "\n";

    // myfile.close();
    std::cout << "[MAIN ENTRY] ALL Functions count : " << all_funcs.size() << "\n";
    std::cout << "[MAIN ENTRY] STD Functions count : " << std_funcs.size() << "\n";
    std::cout << "[MAIN ENTRY] Only Source Code Functions count : " << all_funcs.size() - std_funcs.size() << "\n";
    std::cout << "[MAIN ENTRY] ALL BB count : [" << i << "]" << "\n";
    std::cout << "[MAIN ENTRY] STD BB count : [" << j << "]" << "\n";
    std::cout << "[MAIN ENTRY] Only Source Code BB count : [" << i-j << "]" << "\n";

    *allbb = i;

    return all_BB;

}

std::set<const BasicBlock*> countAllBBAsyncSafe(SVFModule* svfModule, ICFG* icfg, int* allbb ,size_t fuzzing_options) {
    const Instruction* main_inst = get_main_inst(svfModule, fuzzing_options);
    // const Instruction* main_inst = get_target_inst("main", svfModule);
    ICFGNode* mainNode = icfg->getBlockICFGNode(main_inst);
    FIFOWorkList<const ICFGNode*> worklist;
    std::set<ICFGEdge*> visited;
    std::set<const Function*> all_funcs;
    std::set<const BasicBlock*> all_BB;

    std::set<const Function*> std_funcs;
    std::set<const BasicBlock*> std_BB;
    worklist.push(mainNode);

    // ofstream myfile ("countAllBB.txt");

    // if(!myfile.is_open()) {
    //     std::cout << "could not open output file exiting";
    //     exit(1);
    // }

    /// Traverse along ICFG
    while(!worklist.empty())
    {
        const ICFGNode* vNode = worklist.pop();
        for(ICFGNode::const_iterator it = vNode->OutEdgeBegin(), eit =
                vNode->OutEdgeEnd(); it != eit; ++it) {
            ICFGEdge* edge = *it;
            ICFGNode* nextNode = edge->getDstNode();
            const SVFFunction* nextFun = nextNode->getFun();
            if (visited.find(edge) == visited.end()) {

                // Std library BB blocks
                if(nextFun && (isBlacklisted(nextFun->getLLVMFun()) || isDroplisted(nextFun->getLLVMFun()) || isAsynclisted(nextFun->getLLVMFun()))) {   
                    if(std_funcs.find(nextFun->getLLVMFun()) == std_funcs.end()) {
                        std_funcs.insert(nextFun->getLLVMFun());
                    }

                    const BasicBlock* stdbb = nextNode->getBB();
                    if(stdbb != nullptr) {
                        if(std_BB.find(stdbb) == std_BB.end()) {
                            std_BB.insert(stdbb);
                        }
                    }
                }
                
                // All BB blocks
                if(all_funcs.find(nextFun->getLLVMFun()) == all_funcs.end()) {
                    all_funcs.insert(nextFun->getLLVMFun());
                }
                const BasicBlock* bb = nextNode->getBB();
                if(bb != nullptr) {
                    if(all_BB.find(bb) == all_BB.end()) {
                        all_BB.insert(bb);
                    }
                }

                worklist.push(nextNode);
                visited.insert(edge);

            }
        }
    }


    int j = 0;
    int i = 0;
    for(const auto & bb : all_BB) {
        i++;
    }

    for(const auto & bb : std_BB) {
        j++;
    }

    // myfile << "ALL BB count : [" << i << "]" << "\n";
    // myfile << "STD BB count : [" << j << "]" << "\n";
    // myfile << "Only Source Code BB count : [" << i-j << "]" << "\n";

    // myfile.close();
    std::cout << "[MAIN ENTRY] ALL Functions count : " << all_funcs.size() << "\n";
    std::cout << "[MAIN ENTRY] STD or Async Functions count : " << std_funcs.size() << "\n";
    std::cout << "[MAIN ENTRY] Only Source Code Functions count : " << all_funcs.size() - std_funcs.size() << "\n";
    std::cout << "[MAIN ENTRY] ALL BB count : [" << i << "]" << "\n";
    std::cout << "[MAIN ENTRY] STD or Async BB count : [" << j << "]" << "\n";
    std::cout << "[MAIN ENTRY] Only Source Code BB count : [" << i-j << "]" << "\n";

    *allbb = i;

    return all_BB;

}

void countAllBB_debug(SVFModule* svfModule, Module* llvmModule, ICFG* icfg, size_t fuzzing_options) {
    const Instruction* main_inst = get_main_inst(svfModule, fuzzing_options);
    // const Instruction* main_inst = get_target_inst("main", svfModule);
    ICFGNode* mainNode = icfg->getBlockICFGNode(main_inst);
    FIFOWorkList<const ICFGNode*> worklist;
    std::set<ICFGEdge*> visited;
    std::set<const BasicBlock*> all_BB;
    std::set<const BasicBlock*> std_BB;
    worklist.push(mainNode);

    int x = 0;

    for (auto &func : *llvmModule) {
        for (auto &bb : func) {
            x++;
        }
    }

    std::cout << "[TOTAL] Module BB count : [" << x << "]" << "\n";

    /// Traverse along ICFG
    while(!worklist.empty())
    {
        const ICFGNode* vNode = worklist.pop();
        for(ICFGNode::const_iterator it = vNode->OutEdgeBegin(), eit =
                vNode->OutEdgeEnd(); it != eit; ++it) {
            ICFGEdge* edge = *it;
            ICFGNode* nextNode = edge->getDstNode();
            const SVFFunction* nextFun = nextNode->getFun();
            if (visited.find(edge) == visited.end()) {

                // Std library BB blocks
                if(nextFun && (isBlacklisted(nextFun->getLLVMFun()) || isDroplisted(nextFun->getLLVMFun()))) {   
                    const BasicBlock* stdbb = nextNode->getBB();
                    if(stdbb != nullptr) {
                        if(std_BB.find(stdbb) == std_BB.end()) {
                            std_BB.insert(stdbb);
                        }
                    }
                }
 
                const BasicBlock* bb = nextNode->getBB();
                if(bb != nullptr) {
                    if(all_BB.find(bb) == all_BB.end()) {
                        all_BB.insert(bb);
                    }
                }

                worklist.push(nextNode);
                visited.insert(edge);

            }
        }
    }

    int j = 0;
    int i = 0;
    for(const auto & bb : all_BB) {
        i++;
    }

    for(const auto & bb : std_BB) {
        j++;
    }

    std::cout << "[MAIN ENTRY] ALL BB count : [" << i << "]" << "\n";
    std::cout << "[MAIN ENTRY] STD BB count : [" << j << "]" << "\n";
    std::cout << "[MAIN ENTRY] Only Source Code BB count : [" << i-j << "]" << "\n";

}


void countReachableBB(SVFModule* svfModule, ICFG* icfg) {
    const Instruction* main_inst = get_main_inst(svfModule, 0);
    ICFGNode* mainNode = icfg->getBlockICFGNode(main_inst);
    FIFOWorkList<const ICFGNode*> worklist;
    std::set<ICFGEdge*> visited;
    std::set<const BasicBlock*> visited_BB;
    worklist.push(mainNode);

    int i = 0;
    ofstream myfile ("countReachableBB.txt");
    if(!myfile.is_open()) {
        std::cout << "could not open output file exiting";
        exit(1);
    }
    /// Traverse along ICFG
    while(!worklist.empty())
    {
        const ICFGNode* vNode = worklist.pop();
        for(ICFGNode::const_iterator it = vNode->OutEdgeBegin(), eit =
                vNode->OutEdgeEnd(); it != eit; ++it) {
            ICFGEdge* edge = *it;
            ICFGNode* nextNode = edge->getDstNode();
            if (visited.find(edge) == visited.end()) {
                worklist.push(nextNode);
                visited.insert(edge);
                if(!nextNode->getAllow()) {
                    const BasicBlock* bb = nextNode->getBB();
                    if(visited_BB.find(bb) == visited_BB.end()) {
                        visited_BB.insert(bb);
                    }
                }

            }
        }
    }

    for(const auto & bb : visited_BB) {
        i++;
        if(bb->hasName()) {
            myfile << "[" << i << "]" << "BB NAME : " << bb->getName().str() << "\n";
        }
        else {
            myfile << "[" << i << "]" << "BB NAME : " << "(anonymous)" << "\n";
        }
        myfile << "[" << i << "]" << "Instruction Size : " << bb->size() << "\n";
    }

    myfile << "Reachable BB count : [" << i << "]" << "\n";
    myfile.close();
    
}



std::set<std::string> traverseCallGraph(
        PTACallGraph* callgraph, 
        PTACallGraphNode* target) {

	FIFOWorkList<PTACallGraphNode*> worklist_backward; // Worklist for doing the backwards traversal
    FIFOWorkList<PTACallGraphNode*> worklist_forward; // Worklist for doing the forward traversal
    std::set<PTACallGraphNode*> visited;
    worklist_backward.push(target);
    std::set<std::string> visited_functions; // Holds the allowlisted functions
	while (!worklist_backward.empty()) {

		PTACallGraphNode* dst = worklist_backward.pop();
        // Find all hard dependencies for the candidate and
        // - add them to the worklist
        // - add the functions to the allowlist 
        for (PTACallGraphEdge::CallGraphEdgeSet::iterator it = dst->InEdgeBegin();
               it != dst->InEdgeEnd(); ++it) {
           PTACallGraphEdge* edge = (*it); 
		   PTACallGraphNode* srcNode = edge->getSrcNode();

           if (visited.find(srcNode) == visited.end()) {

              // Get function name
              const SVFFunction* srcFun = srcNode->getFunction();
              const Function* candidate = srcFun->getLLVMFun();
              
              worklist_backward.push(srcNode);
              visited_functions.insert(candidate->getName().str());
              visited.insert(srcNode);
           }
        }

        // Find all soft dependencies and
        // - add the functions to the allowlist
        // - Recursively add their children to the allowlist
        for (PTACallGraphEdge::CallGraphEdgeSet::iterator it = dst->OutEdgeBegin();
               it != dst->OutEdgeEnd(); ++it) {
           PTACallGraphEdge* edge = (*it); 
		   PTACallGraphNode* dstNode = edge->getDstNode();

              // Get function name
              const SVFFunction* dstFun = dstNode->getFunction();
              const Function* candidate = dstFun->getLLVMFun();

              visited_functions.insert(candidate->getName().str());
              worklist_forward.push(dstNode); 
           }

        // Recursively add all children of soft dependencies.
        while (!worklist_forward.empty()) {

		    PTACallGraphNode* dst = worklist_forward.pop();
            for (PTACallGraphEdge::CallGraphEdgeSet::iterator it = dst->OutEdgeBegin();
                   it != dst->OutEdgeEnd(); ++it) {
               PTACallGraphEdge* edge = (*it); 
		       PTACallGraphNode* dstNode = edge->getDstNode();
               if (visited.find(dstNode) == visited.end()) {
                  // Get function name
                  const SVFFunction* dstFun = dstNode->getFunction();
                  const Function* candidate = dstFun->getLLVMFun();

                  visited_functions.insert(candidate->getName().str());
                  visited.insert(dstNode);
                  worklist_forward.push(dstNode); 
               }
            }
        }
    }

    return visited_functions;
}



std::set<const ICFGNode*> backwardAnalysis(const SVFGNode* sNode, const SVFFunction* mainFun,PTACallGraph* callgraph)
{
    const ICFGNode* iNode = sNode->getICFGNode();
    NodeID iNodeID = iNode->getId();
    std::set<const ICFGNode*> checked_iNodes;
    std::set<const Function*> unreachable_functions;


    checked_iNodes.insert(iNode);
    const SVFFunction* targetFun = iNode->getFun();

    ofstream myfile;
    myfile.open("staticAnalysis.txt", ios::out | ios::app);    

    if(targetFun != nullptr) {
        myfile<<"backwardAnalysis : startNode ["<<iNodeID<<"] - " << iNode->toString() << "/ startFunc ["<<targetFun->getName().str()<<"]\n";
    }


    FIFOWorkList<const ICFGNode*> worklist;
    std::set<ICFGEdge*> visited;
    worklist.push(iNode);

    while(!worklist.empty())
    {
        const ICFGNode* vNode = worklist.pop();

        if(SVFUtil::isa<CallBlockNode>(vNode)) {
            for (ICFGNode::const_iterator it = vNode->OutEdgeBegin(), eit =
                    vNode->OutEdgeEnd(); it != eit; ++it) {
                ICFGEdge* libEdge = *it;
                if(CallCFGEdge* dirCall = SVFUtil::dyn_cast<CallCFGEdge>(libEdge)) {
                    ICFGNode* libNode = libEdge->getDstNode();
                    // Validate that the dest Node has no outgoing edge
                    if(!libNode->hasOutgoingEdge()) {
                        checked_iNodes.insert(libNode);
                    }
                }
            }
        }

        for(ICFGNode::const_iterator it = vNode->InEdgeBegin(), eit = 
                vNode->InEdgeEnd(); it != eit; ++it) {
            ICFGEdge* edge = *it;
            ICFGNode* prevNode = edge->getSrcNode();
            const SVFFunction* prevFun = prevNode->getFun();
            const Function* candidate = prevFun->getLLVMFun();

            if(visited.find(edge) == visited.end()) {

                if(CallCFGEdge* dirCall = SVFUtil::dyn_cast<CallCFGEdge>(edge)) {
                    if((targetFun!=nullptr) && (prevFun!=nullptr)) {
                        // myfile<<"[backwardAnalysis - Function Call starts]\n";
                        if(isReachableForRust(prevFun, targetFun, callgraph)) {
                            checked_iNodes.insert(prevNode);
                            myfile<<"From prevFun["<<prevFun->getName().str()<<"] To targetFun["<<targetFun->getName().str()<<"] is REACHABLE\n";
                        }

                        else {
                            if(unreachable_functions.find(candidate) == unreachable_functions.end()) {
                                myfile << "From prevFun["<<prevFun->getName().str()<<"] to targetFun ["<<targetFun->getName().str()<<"] is UNREACHABLE\n";
                                unreachable_functions.insert(candidate);
                            }
                        }


                    }


                }

                else {
                    if(unreachable_functions.find(candidate) == unreachable_functions.end()) {
                        checked_iNodes.insert(prevNode);
                        myfile << "NOT callEDGE prevFun[" <<prevFun->getName().str() << "]\n";
                    }
                }

                visited.insert(edge);
                worklist.push(prevNode);

            }

        }

    }
    myfile.close();
    return checked_iNodes;

}

void forwardAnalysis(const SVFGNode* sNode, std::set<const SVFGNode*>& visit_svfgUseNodes)
{
    ofstream myfile;
    myfile.open("forwardAnalysis.txt", ios::out | ios::app);
    myfile << "forwardAnalysis : startNode ["<<sNode->getId()<<"] - "<< sNode->toString() << "\n";
    // std::cout << "forwardAnalysis : startFunc ["<<sNode->getFun()->getLLVMFun()->getName().str() <<"]\n";
    FIFOWorkList<const SVFGNode*> worklist;
    std::set<SVFGEdge*> sVisited;
    worklist.push(sNode);


    while(!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
            it != eit; ++it)
        {
            SVFGEdge* edge = *it;
            SVFGNode* nextNode = edge->getDstNode();

            if(sVisited.find(edge) == sVisited.end()){
                s32_t nKind = nextNode->getNodeKind();
                if(nKind == VFGNode::Store || nKind == VFGNode::Load)
                {
                    // myfile << "forwardAnalysis next SVFG node : ["<<nextNode->getId()<<"]\n";
                    // std::cout << "forwardAnalysis next Func ["<<nextNode->getFun()->getLLVMFun()->getName().str()<<"]\n";
                    if(visit_svfgUseNodes.find(nextNode) == visit_svfgUseNodes.end()) {
                         myfile << "forwardAnalysis next node " << nextNode->toString() << "\n";
                        visit_svfgUseNodes.insert(nextNode);
                    }
                }

                worklist.push(nextNode);
                sVisited.insert(edge);

            }

        }

    }

    myfile.close();

}


std::set<const SVFGNode*> getAliasPtr(const SVFG* svfg, const Value* startInst, ofstream &myfile)
{
    const SVFGNode* startNode = *((svfg->fromValue(startInst)).begin());

    myfile << startNode->toString() << "\n";
    FIFOWorkList<const SVFGNode*> worklist;
    std::set<SVFGEdge*> visited;
    worklist.push(startNode);
    std::set<const SVFGNode*> Mems;

    int i = 0;

    while(!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        for(SVFGNode::const_iterator it = vNode->InEdgeBegin(), eit = 
                vNode->InEdgeEnd(); it != eit; ++it) {
            SVFGEdge* edge = *it;

            SVFGNode* prevNode = edge->getSrcNode();

            if(visited.find(edge) == visited.end()){
                worklist.push(prevNode);
                visited.insert(edge);
                if(prevNode->getNodeKind() == VFGNode::Addr && (Mems.find(prevNode) == Mems.end())) {
                    const SVFFunction* prevFunc = prevNode->getFun();
                    if(prevFunc) {
                        if(!isBlacklisted(prevFunc->getLLVMFun())) {
                            Mems.insert(prevNode);
                        }
                    }                  
                }
            }
        }

    }

    
    for(auto ii = Mems.begin(); ii != Mems.end(); ii++) {
        i++;
        myfile << "     " << (*ii)->toString() << "\n";
    }
    myfile << "     " << "Total Memory Locations : [" << i << "]\n\n"; 

    return Mems;
}



void staticAnalysis(const SVFG* svfg, const Instruction* startInst, const SVFFunction* mainFun, PTACallGraph* callgraph)
{
    ofstream myfile;
    myfile.open("staticAnalysis.txt", ios::out | ios::app);    

    std::set<const ICFGNode*> cInodes;
    std::set<const SVFGNode*> useNodes;
    
    std::set<const SVFGNode*> sMems = getAliasPtr(svfg, startInst, myfile); // start Memory Locations

    for(auto i = sMems.begin(); i != sMems.end(); i++)
    {
        const SVFGNode* startMem = *i;
        forwardAnalysis(startMem, useNodes);
    }
    const SVFGNode* use = *(useNodes.begin());
    cInodes = backwardAnalysis(use, mainFun, callgraph);

    std::set<const BasicBlock*> rawPtrRelatedBB;

    ofstream reachableBB;
    reachableBB.open("countReachableBB.txt", ios::out | ios::trunc);

    for(auto ii = cInodes.begin(); ii != cInodes.end(); ii++) {
        const BasicBlock* bb = (*ii)->getBB();
        if(bb != nullptr) {
            if(rawPtrRelatedBB.find(bb) == rawPtrRelatedBB.end()) {
                rawPtrRelatedBB.insert(bb);
            }
        }
    }
    int i =0;
    for(const auto & bb : rawPtrRelatedBB) {
        i++;
        if(bb->hasName()) {
            reachableBB << "[" << i << "]" << "BB NAME : " << bb->getParent()->getName().str() << "-" << bb->getName().str()<< "-" << bb->getFirstNonPHI()->getName().str() << "\n";
        }
        else {
            reachableBB << "[" << i << "]" << "BB NAME : " << "(anonymous)" << "\n";
        }
        reachableBB << "[" << i << "]" << "Instruction Size : " << bb->size() << "\n";
    }

    reachableBB << "ALL reachableBB count : [" << i << "]" << "\n";
    reachableBB.close();


    myfile << "staticAnalysis ends\n";
    myfile << "##########################################################\n\n\n";
    myfile.close();

}


void getOutermostFunctions(PTACallGraphNode* startNode, std::set<const llvm::Function *>& reachableFunctions, std::set<const llvm::Function*>& userDefinedFunctions)
{
    std::set<PTACallGraphNode*> visited;
    const Function* startFun = startNode->getFunction()->getLLVMFun();
    // reachableFunctions.insert(startFun);

    for(PTACallGraphEdge::CallGraphEdgeSet::iterator it = startNode->OutEdgeBegin();
        it != startNode->OutEdgeEnd(); ++it)
    {
        PTACallGraphEdge* edge = (*it);
        PTACallGraphNode* dstNode = edge->getDstNode();
        if(visited.find(dstNode) == visited.end()) {


            const SVFFunction* dstFun = dstNode->getFunction();
            if(!dstFun) continue;
            const Function* candidate = dstFun->getLLVMFun();

            if(reachableFunctions.find(candidate) == reachableFunctions.end() && !(candidate->isDeclaration()) && userDefinedFunctions.find(candidate) == userDefinedFunctions.end()) {
                reachableFunctions.insert(candidate);

            }
            visited.insert(dstNode);
                        
        }
    }



}







std::set<const Function*> callgraphTraverse(PTACallGraphNode* startNode)
{
    FIFOWorkList<PTACallGraphNode*> worklist;
    std::set<PTACallGraphNode*> visited;
    std::set<const Function*> functions;

    ofstream myfile;
    myfile.open("callgraph.txt", ios::out | ios::trunc);

    worklist.push(startNode);

    myfile << "Start Function Name : " << ((startNode->getFunction()->getLLVMFun())->getName()).str() << "\n";
    const Function* startFun = startNode->getFunction()->getLLVMFun();

    functions.insert(startFun);

    while(!worklist.empty())
    {
        PTACallGraphNode* cNode = worklist.pop();
        for(PTACallGraphEdge::CallGraphEdgeSet::iterator it = cNode->OutEdgeBegin();
            it != cNode->OutEdgeEnd(); ++it)
        {
            PTACallGraphEdge* edge = (*it);
            PTACallGraphNode* dstNode = edge->getDstNode();
            if(visited.find(dstNode) == visited.end()) {


                const SVFFunction* dstFun = dstNode->getFunction();
                if(!dstFun) continue;
                const Function* candidate = dstFun->getLLVMFun();

                if(candidate->isDeclaration()) 
                {
                    myfile << "Declare Function : " << candidate->getName().str() << "\n";
                    
                }

                if(functions.find(candidate) == functions.end() && !(candidate->isDeclaration())) {
                    functions.insert(candidate);
                    myfile << "Define Function : " << candidate->getName().str() <<  "\n"; 

                }
                worklist.push(dstNode);
                visited.insert(dstNode);
                            
            }
        }

    }


    myfile.close();
    return functions;

}


std::set<const Function*> TraverseCFGReachableFunc(const ICFGNode* mainNode) 
{
    FIFOWorkList<const ICFGNode*> worklist;
    std::set<const ICFGNode*> visited;
    std::set<const Function*> functions;

    worklist.push(mainNode);

    while(!worklist.empty())
    {
        const ICFGNode* cNode = worklist.pop();
        for(ICFGNode::const_iterator it = cNode->OutEdgeBegin(), eit = cNode->OutEdgeEnd(); it!= eit; ++it)
        {
            const ICFGEdge* edge = *it;
            const ICFGNode* dstNode = edge->getDstNode();
            if(visited.find(dstNode) == visited.end()) {
                const SVFFunction* dstFun = dstNode->getFun();
                if(!dstFun) continue;
                const Function* candidate = dstFun->getLLVMFun();
                if(functions.find(candidate) == functions.end()) {
                    functions.insert(candidate);
                }
                worklist.push(dstNode);
                visited.insert(dstNode);
            }
        }
    }

    return functions;

}



/* rawptr! metadata가 저장된 instruction 중에서 user-provided 함수 내에 존재하는 raw pointer instruction을 찾는다 
    이를 위해 rawptr! metadata instruction에 대해 value-flow graph를 순회한다
*/
//topLevelRawPointer
std::set<const SVFGNode*> TrackAllocSites(const Instruction* inst, std::set<const SVFGEdge*> &backwardVisited, const SVFG* svfg, PTACallGraph* callgraph)
{
    std::set<const SVFGNode*> toplevelRawPtrs;
    const SVFGNode* sNode = *((svfg->fromValue(inst)).begin());
    
    if(sNode == nullptr) {
        return toplevelRawPtrs;
    }
        
    // std::stack<const SVFFunction*> contextWorklist;
    FIFOWorkList<const SVFGNode*> backwardWorklist;
    // std::set<SVFGEdge*> backwardVisited;

    // toplevelRawPtrs.insert(sNode);

    backwardWorklist.push(sNode);
    while(!backwardWorklist.empty())
    {
        const SVFGNode* vNode = backwardWorklist.pop();
        for(SVFGNode::const_iterator it = vNode->InEdgeBegin(), eit = 
                vNode->InEdgeEnd(); it != eit; ++it)
        {

            SVFGEdge* edge = *it;
            if(backwardVisited.find(edge) == backwardVisited.end())
            {

                SVFGNode* prevNode = edge->getSrcNode();
                const SVFFunction* prevFun = prevNode->getFun();
                if(prevFun) {   
                    if(!isBlacklisted(prevFun->getLLVMFun())) {
                        if(prevNode->getNodeKind() == VFGNode::Addr)
                        {
                            toplevelRawPtrs.insert(prevNode);
                        }
                    }
                }
                backwardWorklist.push(prevNode);
                backwardVisited.insert(edge);
            }

        }
    }
    return toplevelRawPtrs;


    /* Handling for blacklist (not analyze the standard library) for fuzzing */
    // if(targetFun)
    // {
    //     const Function* targetLLVMFun = targetFun->getLLVMFun();
    //     if(!isBlacklisted(targetLLVMFun))
    //     {
    //         toplevelRawPtrs.insert(sNode);
    //     }

    // }
    

    // backwardWorklist.push(sNode);
    

    // while(!backwardWorklist.empty())
    // {
    //     const SVFGNode* vNode = backwardWorklist.pop();
    //     for(SVFGNode::const_iterator it = vNode->InEdgeBegin(), eit = 
    //             vNode->InEdgeEnd(); it != eit; ++it) {

    //         SVFGEdge* edge = *it;
    //         SVFGNode* prevNode = edge->getSrcNode();
    //         const SVFFunction* prevFun = prevNode->getFun();


    //         if(backwardVisited.find(edge) == backwardVisited.end()) {
    //             if(CallIndSVFGEdge* IndirCall = SVFUtil::dyn_cast<CallIndSVFGEdge>(edge))
    //             {
    //                 if(ActualParmSVFGNode* actualParmSVFGNode = SVFUtil::dyn_cast<ActualParmSVFGNode>(prevNode))
    //                 {
    //                     const SVFFunction* contextCallFunc = actualParmSVFGNode->getFun();
    //                     if(!contextWorklist.empty()) {
    //                         const SVFFunction* top = contextWorklist.top();
    //                         if(contextCallFunc == top) {
    //                             myfile << "CallEdge : " << prevNode->toString() << "\n";
    //                             myfile << "CallEdge : " << prevNode->getFun()->getName().str() << "\n";
    //                             contextWorklist.pop();
    //                             backwardWorklist.push(prevNode);

    //                         }

    //                     }                        
    //                 }

                    

    //             }
                
    //             else if(CallDirSVFGEdge* dirCall = SVFUtil::dyn_cast<CallDirSVFGEdge>(edge))
    //             {

    //                 if(ActualParmSVFGNode* actualParmSVFGNode = SVFUtil::dyn_cast<ActualParmSVFGNode>(prevNode))
    //                 {
    //                     const SVFFunction* contextCallFunc = actualParmSVFGNode->getFun();
    //                     if(!contextWorklist.empty()) {
    //                         const SVFFunction* top = contextWorklist.top();
    //                         if(contextCallFunc == top) {
    //                             myfile << "CallEdge : " << prevNode->toString() << "\n";
    //                             myfile << "CallEdge : " << prevNode->getFun()->getName().str() << "\n";
    //                             contextWorklist.pop();
    //                             backwardWorklist.push(prevNode);

    //                         }

    //                     }                        
    //                 }

    //             }

    //             else if (RetIndSVFGEdge* RetIndCall = SVFUtil::dyn_cast<RetIndSVFGEdge>(edge)) {
    //                 SVFGNode* RetNode = edge->getDstNode();
    //                 if(ActualRetSVFGNode* actualRetSVFGNode = SVFUtil::dyn_cast<ActualRetSVFGNode>(RetNode))
    //                 {
    //                     const SVFFunction* retFunc = actualRetSVFGNode->getCallSite()->getFun();
    //                     contextWorklist.push(retFunc);
    //                     myfile << "RetEdge : " << RetNode->toString() << "\n";
    //                     myfile << "RetEdge : " << RetNode->getFun()->getName().str() << "\n";
    //                 }
    //                 backwardWorklist.push(prevNode);
    //             }

    //             else if (RetDirSVFGEdge* RetDirCall = SVFUtil::dyn_cast<RetDirSVFGEdge>(edge)) {
    //                 SVFGNode* RetNode = edge->getDstNode();
    //                 if(ActualRetSVFGNode* actualRetSVFGNode = SVFUtil::dyn_cast<ActualRetSVFGNode>(RetNode))
    //                 {
    //                     const SVFFunction* retFunc = actualRetSVFGNode->getCallSite()->getFun();
    //                     contextWorklist.push(retFunc);
    //                     myfile << "RetEdge : " << RetNode->toString() << "\n";
    //                     myfile << "RetEdge CallSite Function : " << retFunc->getName().str() << "\n";
    //                     myfile << "RetEdge : " << RetNode->getFun()->getName().str() << "\n";
    //                 }
    //                 backwardWorklist.push(prevNode);
    //             }

    //             else {
    //                 if(prevFun) {
    //                     // myfile << "Non-CallEdge : " << "Function [" << prevFun->getLLVMFun()->getName().str() << "] ID : " <<prevNode->getId() << "\n";
    //                     if(!isBlacklisted(prevFun->getLLVMFun())) {
    //                         if(prevNode->getNodeKind() == VFGNode::Addr && (toplevelRawPtrs.find(prevNode) == toplevelRawPtrs.end())) {
    //                             toplevelRawPtrs.insert(prevNode);
    //                         }
    //                         // if(prevNode->getNodeKind() == VFGNode::FRet && (toplevelRawPtrs.find(prevNode) == toplevelRawPtrs.end())) {
    //                         //     const Function* exchange_malloc_candidate = 
    //                         //     myfile << "exchange_malloc demangled Name : " ;

    //                         // }
    //                     }
                    
    //                 }
                
    //                 //  myfile << "Non-CallEdge : " << "Function [" << prevFun->getLLVMFun()->getName().str() << "] ID : " <<prevNode->getId() << "\n";
    //                 backwardWorklist.push(prevNode);
  

    //             }

    //             backwardVisited.insert(edge);


    //         }


    //     }
    // }


    

    // return toplevelRawPtrs;




}


bool contains(const std::vector<std::set<const SVFGNode*>> &vec, const std::set<const SVFGNode*>& s) {
    for (const auto& existingSet : vec) {
        if(existingSet == s) {
            return true;
        }
    }
    return false;
}


std::string printPts(PointerAnalysis* pta, const Value* val)
{

    std::string str;
    raw_string_ostream rawstr(str);

    NodeID pNodeId = pta->getPAG()->getValueNode(val);
    rawstr << "pNodeId: " <<pNodeId << "\n";


    const PointsTo& pts = pta->getPts(pNodeId);
    for (PointsTo::iterator ii = pts.begin(), ie = pts.end();
            ii != ie; ii++)
    {
        rawstr << " " << *ii << " ";
        PAGNode* targetObj = pta->getPAG()->getPAGNode(*ii);
        // targetObj->getValue();
        if(targetObj->hasValue())
        {
            rawstr << "(" <<*targetObj->getValue() << ")\t ";
        }

    }

    return rawstr.str();

}

std::vector<std::set<const SVFGNode*>> RawPtrInstructions(Module* llvmModule, const SVFG* svfg, PTACallGraph* callgraph)
{

    std::vector<std::set<const SVFGNode*>> raw_instructions;
    std::set<const SVFGEdge*> visited;

    for (auto &function : *llvmModule) {
        for (auto &bb : function) {
            for (auto &inst : bb) {
                if(inst.getMetadata("rawptr")) {
                    const Function* func = inst.getParent()->getParent();
                    
                    if(!isBlacklisted(func)) {

                        string func_name = demangleString(func->getName().str().c_str());
                        // std::cout << func_name << "\n";
                        //topLevelRawPointer
                        std::set<const SVFGNode*> instructionSet = TrackAllocSites(&inst, visited, svfg, callgraph);
                        if(!contains(raw_instructions, instructionSet) && !(instructionSet.empty())) {
                            raw_instructions.push_back(instructionSet);
                        }
                    }                    

                    
                }
            }
        }
    }

    // int a = 0;

    // for(auto i = raw_instructions.begin(); i != raw_instructions.end(); i++)
    // {
    //     a++;
    //    std::set<const SVFGNode*> inst_set = *i;
    //    std::cout << "Toggle Points ["<<a<<"]" << "\n";
    //    for(auto ii = inst_set.begin(); ii != inst_set.end(); ii++)
    //    {
    //     const SVFGNode* sNode = *ii;
    //     std::cout << "Instruction Set : " << sNode->toString() << "\n";
    //    }
        
    // }

    // std::cout << "Total Toggle Points : " << a << "\n"; 

    return raw_instructions;




}

/* Version 0 */
/*
    Debugging for C/C++ examples
*/

void forwardAnalysis_V0(int startNode, SVFG * svfg, PTACallGraph* callgraph)
{
    ofstream myfile;
    myfile.open("forward_V0.txt", ios::out | ios::app);
    const SVFGNode* sNode = svfg->getSVFGNode(startNode);
    const SVFFunction* stackFun = sNode->getFun();


    myfile << "startNode : " << startNode << " Func : "<<stackFun->getLLVMFun()->getName().str() << "\n";
    FIFOWorkList<const SVFGNode*> worklist;
    std::set<const SVFGEdge*> visited;
    worklist.push(sNode);
    while(!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd(); it!= eit; ++it)
        {
            const SVFGEdge* edge = *it;
            if (visited.find(edge) == visited.end())
            {
                const SVFGNode* nextNode = edge->getDstNode();
                visited.insert(edge);
                worklist.push(nextNode);
                // myfile << "Forward Following : " << nextNode->toString() << "\n";
                if(const SVF::FormalRetSVFGNode* retNode = llvm::dyn_cast<SVF::FormalRetSVFGNode>(nextNode))
                {
                    // myfile << "RetBlockNode : " << retNode->toString() << "\n";
                    const SVFFunction* retFun = retNode->getFun();
                    if(retFun == stackFun)
                    {
                        myfile << "RetEdge : " << "Function [" << retFun->getName().str() << "] ID : " <<nextNode->getId() << "\n";
                    }
                }

            }
        }
    }

    

}


std::vector<ICFGPath> getPathsToTarget(
    const SVF::ICFG *icfg,
    const SVF::ICFGNode *entryNode,
    const SVF::ICFGNode *targetNode)
{
    std::vector<ICFGPath> allPaths;
    std::stack<const SVF::ICFGNode*> dfsStack;
    std::vector<const llvm::Function*> currentPath;
    std::set<const SVF::ICFGNode*> visited;

    dfsStack.push(entryNode);

    while (!dfsStack.empty()) {
        const SVF::ICFGNode* currentNode = dfsStack.top();

        if (visited.count(currentNode)) {
            dfsStack.pop();
            if (!currentPath.empty())
                currentPath.pop_back();
            continue;
        }

        visited.insert(currentNode);

        if (const SVFFunction* svfFun = currentNode->getFun()) {
            if (const llvm::Function* func = svfFun->getLLVMFun()) {
                currentPath.push_back(func);
            }
        }

        if (currentNode == targetNode) {
            allPaths.push_back(currentPath);

            if (!currentPath.empty())
                currentPath.pop_back();
            dfsStack.pop();
            continue;
        }

        bool hasChildren = false;
        for (SVF::ICFGNode::const_iterator eit = currentNode->OutEdgeBegin();
             eit != currentNode->OutEdgeEnd(); ++eit)
        {
            SVF::ICFGEdge* edge = *eit;
            if (!edge) continue;

            const SVF::ICFGNode* nextNode = edge->getDstNode();
            if (visited.count(nextNode) == 0) {
                dfsStack.push(nextNode);
                hasChildren = true;
            }
        }

        if (!hasChildren) {
            if (!currentPath.empty())
                currentPath.pop_back();
            dfsStack.pop();
        }
    }

    return allPaths;
}
// std::vector<ICFGPath> getPathsToTarget(
//     const SVF::ICFG *icfg,
//     const SVF::ICFGNode *entryNode,
//     const SVF::ICFGNode *targetNode)
// {
//     std::vector<ICFGPath> allPaths;
//     std::stack<std::pair<const SVF::ICFGNode*, std::vector<const llvm::Function*>>> dfsStack;
//     std::set<std::pair<const SVF::ICFGNode*, std::vector<const llvm::Function*>>> visited;

//     dfsStack.push({entryNode, {}});

//     while (!dfsStack.empty()) {
//         auto currentPair = dfsStack.top();
//         const SVF::ICFGNode* currentNode = currentPair.first;
//         std::vector<const llvm::Function*> currentPath = currentPair.second;

//         dfsStack.pop();

//         if (visited.count({currentNode, currentPath})) {
//             continue;
//         }
//         visited.insert({currentNode, currentPath});

//         if (const SVFFunction* svfFun = currentNode->getFun()) {
//             if (const llvm::Function* func = svfFun->getLLVMFun()) {
//                 currentPath.push_back(func);
//             }
//         }

//         if (currentNode == targetNode) {
//             allPaths.push_back(currentPath);
//             continue;
//         }

//         for (auto *edge : currentNode->getOutEdges()) {
//             const SVF::ICFGNode* nextNode = edge->getDstNode();
//             if (visited.count({nextNode, currentPath}) == 0) {
//                 auto newPath = currentPath; 
//                 dfsStack.push({nextNode, newPath});
//             }
//         }
//     }

//     return allPaths;
// }


void analyzeICFGWithPaths(
    SVFModule* svfModule,
    ICFG * icfg,
    /*SVF::PTACallGraph* callgraph,*/
    const std::string& entryFunctionName,
    const std::string& targetFunctionName)
{

    const SVF::ICFGNode* entryNode = nullptr;
    const SVF::ICFGNode* targetNode = nullptr;

    // for (auto it = callgraph->begin(); it != callgraph->end(); ++it) {
    //     // it->first : NodeID
    //     // it->second : PTACallGraphNode*
    //     const SVF::PTACallGraphNode* node = it->second;
    //     if (!node) continue;

    //     if (const llvm::Function* func = node->getFunction()->getLLVMFun()) {
    //         if (func->getName().str().find(entryFunctionName) != std::string::npos) {
    //             entryNode = node;
    //         }
    //         if (func->getName().str().find(targetFunctionName) != std::string::npos) {
    //             targetNode = node;
    //         }
    //     }
    // }

    for (SVFModule::llvm_const_iterator I = svfModule->llvmFunBegin(), E = svfModule->llvmFunEnd(); I != E; ++I) {
        if((*I)->getName().str().find(entryFunctionName) != std::string::npos) {
            const Function* func = (*I);
            if (func) {
                const BasicBlock& BB = func->getEntryBlock();
                if(!BB.empty()) {
                    const Instruction* inst = &(BB.front());
                    entryNode = icfg->getBlockICFGNode(inst);
                }
            }
        }
        if((*I)->getName().str().find(targetFunctionName) != std::string::npos) {
            const Function* func = (*I);
            if (func) {
                const BasicBlock& BB = func->getEntryBlock();
                if(!BB.empty()) {
                    const Instruction* inst = &(BB.front());
                    targetNode = icfg->getBlockICFGNode(inst);
                }
            }
        }
    }

    if (!entryNode) {
        std::cerr << "Entry function '" << entryFunctionName << "' not found.\n";
        return;
    }
    if (!targetNode) {
        std::cerr << "Target function '" << targetFunctionName << "' not found.\n";
        return;
    }

    auto paths = getPathsToTarget(icfg, entryNode, targetNode);

    ofstream myfile;
    myfile.open("analyzeICFGWithPaths.txt", ios::out | ios::trunc);
    myfile << "Paths from " << entryFunctionName << " to "
              << targetFunctionName << ":\n";

    int a = 0;
    for (const auto &path : paths) {
        a++;
        myfile << "Path [" << a << "]\n";
        for (auto *f : path) {
            myfile << f->getName().str() << " -> \n";
        }
        myfile << "(target)\n\n";
    }

    myfile.close();
}






/* Version 0 */
/*
    Debugging for C/C++ examples
*/

void backwardAnalysis_V0(int startNode, ICFG * icfg, PTACallGraph* callgraph)
{
    ofstream myfile;
    myfile.open("backward_V0.txt", ios::out | ios::trunc);
    
    // const ICFGNode* iNode = icfg->getBlockICFGNode(inst);
    // NodeID startNode = iNode->getId();
    const ICFGNode* iNode = icfg->getICFGNode(startNode);
    const SVFFunction* targetFun = iNode->getFun();


    // myfile << "startNode : " << startNode << " Func : "<<targetFun->getLLVMFun()->getName().str() << "\n";
    std::set<const ICFGNode*> checked_iNodes;
    FIFOWorkList<const ICFGNode*> worklist;
    std::set<const ICFGEdge*> visited;
    worklist.push(iNode);
    
    while(!worklist.empty())
    {
        const ICFGNode* vNode = worklist.pop();
        const SVFFunction* vNodeFun = vNode->getFun();
        if(vNodeFun) {
            const Function* vFun = vNodeFun->getLLVMFun();
        }
        

        for (ICFGNode::const_iterator it = vNode->InEdgeBegin(), eit = 
                vNode->InEdgeEnd(); it != eit; ++it)
        {
            ICFGEdge* edge = *it;
            ICFGNode* prevNode = edge->getSrcNode();
            
            const SVFFunction* prevFun = prevNode->getFun();
            // const Function* candidate = prevFun->getLLVMFun();

            if(visited.find(edge) == visited.end())
            {
                if(CallCFGEdge* dirCall = SVFUtil::dyn_cast<CallCFGEdge>(edge)) {
                    myfile << "Call Edge Node : " << prevNode->getId() << "\n";
                    if(isReachableForRust(prevFun, targetFun, callgraph)) {
                        worklist.push(prevNode);
                        checked_iNodes.insert(prevNode);
                        if(prevFun) {
                            const Function* candidate = prevFun->getLLVMFun();
                            myfile << targetFun->getLLVMFun()->getName().str() << " is reachable by " << candidate->getName().str() << "\n";
                        }
                        
                    }
                    else {
                        if(prevFun) {
                            const Function* candidate = prevFun->getLLVMFun();
                            myfile << targetFun->getLLVMFun()->getName().str() << " is not reachable by " << candidate->getName().str() << "\n";
                   
                        
                        }
                        
                    }
                }
                
                else if(RetCFGEdge* retCall = SVFUtil::dyn_cast<RetCFGEdge>(edge)) {
                    myfile << "Ret Edge Node : " << prevNode->getId() << "\n";
                    if(prevFun) {
                        const Function* candidate = prevFun->getLLVMFun();
                        if(isBlacklisted(candidate)) {
                            myfile << prevNode->getId()<< " "<< candidate->getName().str() << " is not analyzed\n";
                        }
                        else {
                            worklist.push(prevNode);
                            checked_iNodes.insert(prevNode);
                            myfile << prevNode->getId() << " " << candidate->getName().str() << " is analyzed\n";
                        }
                    }
                }

                else {
                    if(prevFun) {
                        myfile << "Non Call Edge Node : " << prevNode->getId() << " Funcs : " << prevFun->getLLVMFun()->getName().str() << "\n"; 
                    }
                    
                    worklist.push(prevNode);
                    checked_iNodes.insert(prevNode);
                       
                }
                visited.insert(edge);
            }
        }

    }


    // std::set<const BasicBlock*> RAW_BB;
    // int reachable = 0;
    // for(auto ci = checked_iNodes.begin(); ci != checked_iNodes.end(); ci++){
    //     const BasicBlock* bb = (*ci)->getBB();
    //     if(bb!=nullptr) {
    //         if(allBasicBlocks.find(bb) != allBasicBlocks.end()) { 
    //             if(RAW_BB.find(bb) == RAW_BB.end()) {
    //                 reachable++;
    //                 RAW_BB.insert(bb);
    //                 myfile << "Basic Block : " << bb->getParent()->getName().str() << " : " << bb->getName().str() << "\n";
    //             }
    //         }
    //     }
    // }
    

}






/* Version 1 */
/* 
    Toggle Points : Raw Pointer Assignment
    Prune Criteria : BB 
    Algorithm :
    (1) Raw Pointer Assignment Instrumentation 부분을 찾는다 : getRawPtrAssignInstrumentation -> std::set<ICFGNode*>
    (2) Raw Pointer Assignment Instrumentation 으로부터 Raw Pointer Instruction을 찾는다 : getRawPtrAssignInst -> const Instruction*
    == RawPtrInstructions -> std::set<const Instruction*>
    (3) Raw Pointer Instruction 으로부터 alias pointer의 Def Site를 찾는다 : getAliasPtr -> std::set<const SVFGNode*>
    (4) 모든 alias pointer의 Def Site로부터 Backward Reachability Analysis를 진행한다 : staticAnalysis_V1 / backwardAnalysis_V1
    (5) 모든 alias pointer의 Def Site로부터 모든 Use Case를 찾는다 : staticAnalysis_V1 / forwardAnalysis_V1
    (6) 모든 Use Case로부터 다시 Backward Reachability Analysis를 진행한다 : staticAnalysis_V1 / backwardAnalysis_V1
*/

void backwardAnalysis_V1(const SVFGNode* sNode, std::set<const ICFGNode*>& checked_iNodes, std::set<ICFGEdge*>& ivisited, PTACallGraph* callgraph, ofstream &myfile)
{
    const ICFGNode* iNode = sNode->getICFGNode();
    NodeID iNodeID = iNode->getId();
    checked_iNodes.insert(iNode);
    FIFOWorkList<const ICFGNode*> worklist;
    const SVFFunction* targetFun = iNode->getFun();
    
    worklist.push(iNode);

   myfile << "backwardAnalysis_V1 starts " << iNode->toString()  << "\n";


    while(!worklist.empty())
    {
        const ICFGNode* vNode = worklist.pop();

        if(SVFUtil::isa<CallBlockNode>(vNode)) {
            for (ICFGNode::const_iterator it = vNode->OutEdgeBegin(), eit =
                    vNode->OutEdgeEnd(); it != eit; ++it) {
                ICFGEdge* libEdge = *it;
                if(CallCFGEdge* dirCall = SVFUtil::dyn_cast<CallCFGEdge>(libEdge)) {
                    ICFGNode* libNode = libEdge->getDstNode();
                    // Validate that the dest Node has no outgoing edge
                    if(!libNode->hasOutgoingEdge()) {
                        checked_iNodes.insert(libNode);
                    }
                }
            }
        }

        for(ICFGNode::const_iterator it = vNode->InEdgeBegin(), eit =
                vNode->InEdgeEnd(); it != eit; ++it) {
            ICFGEdge* edge = *it;
            ICFGNode* prevNode = edge->getSrcNode();
            const SVFFunction* prevFun = prevNode->getFun();
            // const Function* candidate = prevFun->getLLVMFun();

            if(ivisited.find(edge) == ivisited.end()) {

                if(CallCFGEdge* dirCall = SVFUtil::dyn_cast<CallCFGEdge>(edge)) {
                    
                    if(isReachableForRust(prevFun, targetFun, callgraph)) {
                        if((targetFun!=nullptr) && (prevFun!=nullptr)) {
                            if(!isBlacklisted(prevFun->getLLVMFun())) {
                                myfile << "CallEdge : From prevFun["<<prevFun->getName().str()<<"] To targetFun["<<targetFun->getName().str() << "] is REACHABLE\n";
                                checked_iNodes.insert(prevNode);
                                worklist.push(prevNode);                            
                            }

                            
                        }

                        
                    }
                    else {
                        if((targetFun!=nullptr) && (prevFun!=nullptr)) {

                            myfile << "CallEdge : From prevFun["<<prevFun->getName().str()<<"] To targetFun["<<targetFun->getName().str() << "] is UNREACHABLE\n";
                        }
                    }
                    
                }

                else if (RetCFGEdge* retCall = SVFUtil::dyn_cast<RetCFGEdge>(edge)) {
                    if((targetFun!=nullptr) && (prevFun!=nullptr)) {
                        const Function* candidate = prevFun->getLLVMFun();
                        if(isBlacklisted(candidate)) {
                            myfile << prevNode->getId() << " " << candidate->getName().str() << " is not analyzed\n";
                        }
                        else {
                            worklist.push(prevNode);
                            checked_iNodes.insert(prevNode);
                            myfile << prevNode->getId() << " " << candidate->getName().str() << " is analyzed\n";
                        }
                    }
                }



                else {
                    if(prevFun) {
                        myfile << "Non-CallEdge : " << "Function [" << prevFun->getLLVMFun()->getName().str() << "] ID : " <<prevNode->getId() << "\n";
                    }
                    
                    worklist.push(prevNode);
                    checked_iNodes.insert(prevNode);
                }

                ivisited.insert(edge);

            }
            
        }

    }

    myfile << "backwardAnalysis_V1 ends\n";

}


void forwardAnalysis_V1(const SVFGNode* sNode, std::set<const ICFGNode*>& checked_iNodes, std::set<ICFGEdge*>& ivisited, std::set<SVFGEdge*>& svisited, PTACallGraph* callgraph, ofstream &myfile)
{
    
    myfile << "forwardAnalysis_V1 Func [" << sNode->getFun()->getName().str() << "]" << "\n";
    myfile << "forwardAnalysis_V1 Instruction " << sNode->toString() << "\n";


    FIFOWorkList<const SVFGNode*> worklist;
    worklist.push(sNode);

    while(!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
            it != eit; ++it)
        {
            SVFGEdge* edge = *it;
            SVFGNode* nextNode = edge->getDstNode();

            const SVFFunction* nextFun = nextNode->getFun();
            if(svisited.find(edge) == svisited.end()) {
                s32_t nKind = nextNode->getNodeKind();
                if(nKind == VFGNode::Store || nKind == VFGNode::Load)
                {
                    if(nextFun) {
                        if(!isBlacklisted(nextFun->getLLVMFun()))
                        {
                            myfile <<"\n";



                            backwardAnalysis_V1(nextNode, checked_iNodes, ivisited, callgraph, myfile);
                        }
                    }
                   
                }

                worklist.push(nextNode);
                svisited.insert(edge);

            }
        }
    }


    // myfile << "forwardAnalysis_V1 ends\n";
}





void staticAnalysis_V1(SVFModule* svfModule ,Module* module, const SVFG* svfg, ICFG* icfg, PTACallGraph* callgraph)
{
    ofstream myfile;
    myfile.open("staticAnalysis_V1.txt", ios::out | ios::app);

    // /*
    //     Function : RawPtrInstructions -> std::set<const Instruction*>
    // */

    int allbb = 0;
    std::vector<std::set<const SVFGNode*>> raw_inst = RawPtrInstructions(module, svfg, callgraph);
    std::set<const BasicBlock*> allBasicBlocks = countAllBB(svfModule, icfg, &allbb, 0);
    int toggle_count = 0;

    // // // int break_count = 0;

    for(auto ii = raw_inst.begin(); ii != raw_inst.end(); ii++) // Raw Pointer - Toggle Points
    {
        std::set<const ICFGNode*> checked_iNodes;
        
        std::set<ICFGEdge*> ivisited;
        std::set<SVFGEdge*> svisited;
        std::set<const Function*> useBlackListFuncs;
        std::set<const SVFGNode*> toplevelraws = *ii;

        toggle_count++;
        
        myfile << "Toggle Points ["<<toggle_count<<"]" << "\n";
        myfile << "Toggle Points instructions \n";

        for(auto s = toplevelraws.begin(); s != toplevelraws.end(); s++)
        {
            const SVFGNode* topPtr = *s;
            
            forwardAnalysis_V1(topPtr, checked_iNodes, ivisited, svisited, callgraph, myfile);

        }

        // forwardAnalysis_V1(inst_set, svfg, checked_iNodes, ivisited, callgraph, useBlackListFuncs, myfile);
        
        /*
            Function : getAliasPtr -> std::set<const SVFGNode*>
        */   

        /*
            Function : backwardAnalysis_V1
        */


    /*
    */        

        int reachable = 0;
        std::set<const BasicBlock*> RAW_BB;
         

        for(auto ci = checked_iNodes.begin(); ci != checked_iNodes.end(); ci++){
            const BasicBlock* bb = (*ci)->getBB();
            if(bb!=nullptr) {
                if(allBasicBlocks.find(bb) != allBasicBlocks.end()) { 
                    if(RAW_BB.find(bb) == RAW_BB.end()) {
                        reachable++;
                        RAW_BB.insert(bb);
                        myfile  << "Basic Block : " << bb->getParent()->getName().str() << " : " << bb->getName().str() << "\n";
                    }
                }
            }
        }
        

        myfile << "Toggle Points ["<<toggle_count<<"] Reachable BB : " << reachable << "\n";
        // break_count++;
        // if(break_count == 100) break;
    }


    myfile.close();

}



/* ---------------------------------------------------------------------------------------------------------------------------------- */

/* Rust Sanitizer Static Analysis - Erasan*/
/* 
    (1) Raw Pointer Assignment Finding - RawPtrInstructions
    (2) All Top Pointers and Objects that Raw Pointer Points to - TrackAllocSites
    (3) Find all use(Load/Store) cases from Top Pointers and Objects - TrackAccessSites
    (4) Whole Static Analysis that contains (1)~(3) - ErasanAnalysis 
*/


std::set<const SVFGNode*> SanitizerRawPtrInstructions(Module* llvmModule, const SVFG* svfg, PTACallGraph* callgraph)
{

    // std::vector<std::set<const SVFGNode*>> raw_instructions;
    
    std::set<const SVFGNode*> raw_allocation_site;
    std::set<const Instruction*> raws;



    for (auto &function : *llvmModule) {
        for (auto &bb : function) {
            for (auto &inst : bb) {
                if(inst.getMetadata("rawptr") || inst.getMetadata("rawptr_deref")) {
                    
                    // If rawptr metadata instruction is allocation site itself
                    if(const AllocaInst* AI = dyn_cast<AllocaInst>(&inst))
                    {
                        const SVFGNode* sNode = *((svfg->fromValue(AI)).begin());
                        if(sNode != nullptr) {
                            raw_allocation_site.insert(sNode);
                        }
                    }
                    
                    // If rawptr metadata instruction is not allocation site instruction
                    else {
                        if(raws.find(&inst) == raws.end()) {
                            raws.insert(&inst);
                        }
                    }

                }
            }
        }
    }

    std::cout << "\n ******Finished Iterating through all instructions******\n";

    // Track Allocation Sites from raw pointer instructions
    std::set<const SVFGEdge*> visited;
    for(auto a = raws.begin(); a!= raws.end(); a++)
    {
        const Instruction* inst = *a;
        std::set<const SVFGNode*> instructionSet = TrackAllocSites(inst, visited, svfg, callgraph);
        for(auto i = instructionSet.begin(); i != instructionSet.end(); i++)
        {
            const SVFGNode* topObj = *i;
            if(raw_allocation_site.find(topObj) == raw_allocation_site.end())
            {
                raw_allocation_site.insert(topObj);
            }
        }       
    }

    std::cout << "\n ******Finished Tracking Allocation Sites******\n";

    return raw_allocation_site;

}


//SanitizerForwardAnalysis
void TrackAccessSites(const SVFGNode* sNode, std::set<const SVFGNode*> &checked_sNodes, std::set<const SVFGEdge*>& svisited, ofstream &myfile)
{
    FIFOWorkList<const SVFGNode*> worklist;
    worklist.push(sNode);

    while(!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
            it != eit; ++it)
        {
            SVFGEdge* edge = *it;
            SVFGNode* nextNode = edge->getDstNode();

            const SVFFunction* nextFun = nextNode->getFun();
            if(svisited.find(edge) == svisited.end()) {
                s32_t nKind = nextNode->getNodeKind();
                if(nKind == VFGNode::Store || nKind == VFGNode::Load)
                {
                    if(checked_sNodes.find(nextNode) == checked_sNodes.end())
                    {
                        // myfile << "[topLevel Obj - " <<sNode->toString() << "]\n";
                        // if(nextFun) {
                        //     myfile <<"[Use Instruction - "<<nextFun->getName().str() << "]\n";
                        // }
                        // myfile << "[Use Instruction - " <<nextNode->toString() << "]\n\n\n";
                        checked_sNodes.insert(nextNode);
                    }
                }
                worklist.push(nextNode);
                svisited.insert(edge);

            }
        }
    }

}





void ErasanAnalysis(SVFModule* svfModule, Module* module, const SVFG* svfg, ICFG* icfg, PTACallGraph* callgraph)
{
    ofstream myfile;
    myfile.open("Erasan.txt", ios::out | ios::app);
    std::set<const SVFGNode*> topObjs = SanitizerRawPtrInstructions(module, svfg, callgraph);
    std::set<const SVFGNode*> allRelatedInst;
    std::set<const SVFGEdge*> svisited;
    int a = 0;

    for(auto ii = topObjs.begin(); ii != topObjs.end(); ii++)
    {
        const SVFGNode* topPtr = *ii;
        a++;
        // myfile << "[" << a << "] topLevel Obj : " << topPtr->toString() << "\n\n";
        if(allRelatedInst.find(topPtr) == allRelatedInst.end())
        {
            allRelatedInst.insert(topPtr);
        }
        // SanitizerForwardAnalysis(topPtr, allRelatedInst, svisited, myfile);
        TrackAccessSites(topPtr, allRelatedInst, svisited, myfile);

    }

    std::cout << "\n ******Finished Tracking Access Sites******\n";


    for(auto i = allRelatedInst.begin(); i != allRelatedInst.end(); i++)
    {
        const SVFGNode* sNode = *i;
        const ICFGNode* iNode = sNode->getICFGNode();
        

        if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(iNode))
        {
            Instruction* inst = const_cast<Instruction*>(intra->getInst());
            LLVMContext &context = inst->getContext();
            MDString *str = MDString::get(context, "target");
            MDNode *node = MDNode::get(context, str);
            inst->setMetadata("target", node);
        }
        else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode))
        {
            Instruction* inst = const_cast<Instruction*>(callNode->getCallSite());
            LLVMContext &context = inst->getContext();
            MDString *str = MDString::get(context, "target");
            MDNode *node = MDNode::get(context, str);
            inst->setMetadata("target", node);
        }
        else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(iNode))
        {
            Instruction* inst = const_cast<Instruction*>(RetNode->getCallSite());
            LLVMContext &context = inst->getContext();
            MDString *str = MDString::get(context, "target");
            MDNode *node = MDNode::get(context, str);
            inst->setMetadata("target", node);
        }


    }

    std::cout << "\n ******Finished Marking Target Metadata******\n";
    myfile.close();
    writeModuleToFile(module, "erasanAnalysis.ll");


}


/* ---------------------------------------------------------------------------------------------------------------------------------- */

// Erasan Opt Version 1


/* Rust Sanitizer Static Analysis - Erasan Opt 1*/
/* 
    (1) Raw Pointer Assignment/Dereference Finding - RawPtrInstructions (!rawptr, !rawptr_deref)
    (2) Raw Pointer Tracking - TrackRawPtrSites (!rawptrAccess)
    (3) All Top Pointers and Objects that Raw Pointer Points to - TrackAllocSites
    (4) Differentiate Heap/Stack Object that Raw Pointer points to - HeapOrStackIdentification (!heap_target, !stack_target)
    (5) Optimize Safe Heap Memory Access (no check before drop) - SafeReferenceOptimization (!afterDropAccess)
    
*/

void HeapOrStackIdentification(const SVFGNode* sNode, std::set<const SVFGNode*> &heapObjs, std::set<const SVFGNode*> &stackObjs, std::set<const SVFGNode*> &heapNodes, std::set<const SVFGNode*> &stackNodes)
{
    std::stack<std::pair<const SVFGNode*, std::vector<const SVFGNode*>>> worklist;
    std::set<const SVFGNode*> visited;
    worklist.push({sNode, {sNode}});

    while(!worklist.empty())
    {
        // auto [vNode, path] = worklist.top();
        std::pair<const SVFGNode*, std::vector<const SVFGNode*>> topElem = worklist.top();
        const SVFGNode* vNode = topElem.first;
        std::vector<const SVFGNode*> &path = topElem.second;
        worklist.pop();

        if(visited.find(vNode)!= visited.end()) continue;

        if(stackNodes.find(vNode)!= stackNodes.end()) {
            continue;
        }
        
        else if(heapNodes.find(vNode)!= heapNodes.end()) {
            heapObjs.insert(sNode);
            for(const SVFGNode* pathNode : path) {
                heapNodes.insert(pathNode);
            }
            return;
        }

        visited.insert(vNode);

        string heapstr = vNode->toString();
        if((heapstr.find("malloc") != std::string::npos) || heapstr.find("drop_in_place") != std::string::npos) {

            heapObjs.insert(sNode);
            for(const SVFGNode* pathNode : path) {
                heapNodes.insert(pathNode);

            }
            return;
        }

        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
            it != eit; ++it)
        {
            SVFGEdge* edge = *it;
            SVFGNode* nextNode = edge->getDstNode();
            if(visited.find(nextNode) == visited.end())
            {
                std::vector<const SVFGNode*> newPath = path;
                newPath.push_back(nextNode);
                worklist.push({nextNode, newPath});
            }

        }
    }

    
    for(auto si = visited.begin(), esi = visited.end(); si != esi; ++si)
    {
        stackNodes.insert(*si);
    }
    stackObjs.insert(sNode);
    

}

// void HeapOrStackIdentification(const SVFGNode* sNode,
//                                std::set<const SVFGNode*> &heapObjs,
//                                std::set<const SVFGNode*> &stackObjs,
//                                std::set<const SVFGNode*> &heapNodes,
//                                std::set<const SVFGNode*> &stackNodes)
// {
//     std::stack<const SVFGNode*> worklist;
//     std::set<const SVFGNode*> visited;
//     worklist.push(sNode);

//     while(!worklist.empty())
//     {
//         const SVFGNode* vNode = worklist.top();
//         worklist.pop();

//         if(visited.find(vNode) != visited.end()) continue;
//         visited.insert(vNode);

//         if (const VFGNode* vfgNode = llvm::dyn_cast<VFGNode>(vNode)) {
//             if (llvm::isa<AllocaInst>(vfgNode->getValue())) {
//                 stackObjs.insert(sNode);
//                 stackNodes.insert(vNode);
//                 return;
//             }
//             else if (llvm::isa<CallInst>(vfgNode->getValue()))
//             {
//                 const CallInst* callInst = llvm::cast<CallInst>(vfgNode->getValue());
//                 const Function* calledFunc = callInst->getCalledFunction();
//                 if (calledFunc)
//                 {
//                     std::string funcName = calledFunc->getName().str();
//                     if (funcName.find("alloc") != std::string::npos ||
//                         funcName.find("__rust_alloc") != std::string::npos)
//                     {
//                         heapObjs.insert(sNode);
//                         heapNodes.insert(vNode);
//                         return;
//                     }
//                 }
//             }
//         }

//         // if (const ObjPN* objNode = llvm::dyn_cast<ObjPN>(pagNode))
//         // {
//         //     const Value* allocSite = objNode->getMemObj()->getAllocSite();
//         //     if (allocSite)
//         //     {
//         //         if (llvm::isa<AllocaInst>(allocSite))
//         //         {
//         //             stackObjs.insert(sNode);
//         //             stackNodes.insert(vNode);
//         //             return;
//         //         }
//         //         else if (llvm::isa<CallInst>(allocSite))
//         //         {
//         //             const CallInst* callInst = llvm::cast<CallInst>(allocSite);
//         //             const Function* calledFunc = callInst->getCalledFunction();
//         //             if (calledFunc)
//         //             {
//         //                 std::string funcName = calledFunc->getName().str();
//         //                 if (funcName.find("alloc") != std::string::npos ||
//         //                     funcName.find("__rust_alloc") != std::string::npos)
//         //                 {
//         //                     heapObjs.insert(sNode);
//         //                     heapNodes.insert(vNode);
//         //                     return;
//         //                 }
//         //             }
//         //         }
//         //     }
//         // }

//         for(auto it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd(); it != eit; ++it)
//         {
//             const SVFGEdge* edge = *it;
//             const SVFGNode* nextNode = edge->getDstNode();
//             if(visited.find(nextNode) == visited.end())
//             {
//                 worklist.push(nextNode);
//             }
//         }
//     }

//     stackObjs.insert(sNode);
// }
void ParallelUserDefinedFunctionsIdentification(const std::set<const llvm::Function*> &subset,
                                                std::mutex &userDefFuncMutex,
                                                std::set<const llvm::Function*> &userDefinedFunctions) {
    std::set<const llvm::Function*> localSet;
    for (auto func : subset) {
        if (func->isDeclaration() || 
            (isBlacklisted(func) && !isCMLibrarylisted(func)) || 
            isDroplisted(func))
            continue;

        localSet.insert(func);
    }

    {
        std::lock_guard<std::mutex> lock(userDefFuncMutex);
        userDefinedFunctions.insert(localSet.begin(), localSet.end());
    }
}


void ParallelHeapOrStackIdentificationWrapper(std::set<const SVFGNode*>&subTopObjs, std::set<const SVFGNode*> &heapObjs, std::set<const SVFGNode*> &stackObjs, std::set<const SVFGNode*> &heapNodes, std::set<const SVFGNode*> &stackNodes, std::mutex& mtx)
{
    for (auto ii = subTopObjs.begin(); ii != subTopObjs.end(); ii++)
    {
        const SVFGNode* topPtr = *ii;
        ParallelHeapOrStackIdentification(topPtr, heapObjs, stackObjs, heapNodes, stackNodes, mtx);
    }
}


void ParallelOutermostFunctionsIdentification(
    const std::set<const llvm::Function*> &subset,
    const std::set<const llvm::Function*> &reachableFunctions,
    const std::set<const llvm::Function*> &userDefinedFunctions,
    std::mutex &outermostFuncMutex,
    std::set<const llvm::Function*> &outermostFunctions,
    std::mutex &outermostFuncNameMutex,
    std::set<std::string> &outermostFunctionsName
) {
    std::set<const llvm::Function*> localOutermostFunctions;
    std::set<std::string> localOutermostFunctionsName;

    for (const Function* userFunc : subset) {
        for (const BasicBlock &BB : *userFunc) {
            for (const Instruction &I : BB) {
                const CallBase *callBase = dyn_cast<CallBase>(&I);
                if (!callBase) continue;

                const Function* calledFunc = callBase->getCalledFunction();
                if(!calledFunc) continue;

                if (reachableFunctions.find(calledFunc) == reachableFunctions.end()) continue;
                if (userDefinedFunctions.find(calledFunc) != userDefinedFunctions.end()) continue;

                localOutermostFunctions.insert(calledFunc);
                localOutermostFunctionsName.insert(calledFunc->getName().str());
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(outermostFuncMutex);
        outermostFunctions.insert(localOutermostFunctions.begin(), localOutermostFunctions.end());
    }

    {
        std::lock_guard<std::mutex> lock(outermostFuncNameMutex);
        outermostFunctionsName.insert(localOutermostFunctionsName.begin(), localOutermostFunctionsName.end());
    }
}


void ParallelHeapOrStackIdentification(const SVFGNode* sNode, std::set<const SVFGNode*> &heapObjs, std::set<const SVFGNode*> &stackObjs, std::set<const SVFGNode*> &heapNodes, std::set<const SVFGNode*> &stackNodes, std::mutex& mtx)
{
    std::stack<std::pair<const SVFGNode*, std::vector<const SVFGNode*>>> worklist;
    std::set<const SVFGNode*> visited;
    worklist.push({sNode, {sNode}});

    while(!worklist.empty())
    {
        // auto [vNode, path] = worklist.top();
        std::pair<const SVFGNode*, std::vector<const SVFGNode*>> topElem = worklist.top();
        const SVFGNode* vNode = topElem.first;
        std::vector<const SVFGNode*> &path = topElem.second;
        worklist.pop();

        

        {
            std::lock_guard<std::mutex> lock(mtx);
            if(visited.find(vNode)!= visited.end()) continue;
            visited.insert(vNode);
            if(stackNodes.find(vNode)!= stackNodes.end()) {
                continue;
            }
            
            else if(heapNodes.find(vNode)!= heapNodes.end()) {
                heapObjs.insert(sNode);
                for(const SVFGNode* pathNode : path) {
                    heapNodes.insert(pathNode);
                }
                return;
            }
        }

        

        string heapstr = vNode->toString();
        if((heapstr.find("malloc") != std::string::npos) || heapstr.find("drop_in_place") != std::string::npos) {
            {
                std::lock_guard<std::mutex> lock(mtx);
                heapObjs.insert(sNode);
                for(const SVFGNode* pathNode : path) {
                    heapNodes.insert(pathNode);
                }
            }
            return;
        }

        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
            it != eit; ++it)
        {
            SVFGEdge* edge = *it;
            SVFGNode* nextNode = edge->getDstNode();
            if(visited.find(nextNode) == visited.end())
            {
                std::vector<const SVFGNode*> newPath = path;
                newPath.push_back(nextNode);
                worklist.push({nextNode, newPath});
            }

        }
    }

    {
        std::lock_guard<std::mutex> lock(mtx);
        for(auto si = visited.begin(), esi = visited.end(); si != esi; ++si)
        {
            stackNodes.insert(*si);
        }
        stackObjs.insert(sNode);
    }

}

void ParallelBackwardAnalysisWrapper(std::set<const ICFGNode*>& subOptimizedTogglePoints, std::set<const BasicBlock*>& withinSameBB, int & total_reachable, int & b, std::mutex& mtx_global_bIndex, ostream &ToggleInfo, ostream &myfile, std::set<const llvm::BasicBlock *> &allBasicBlocks, 
                                SVF::PTACallGraph *callgraph, const llvm::Function *mainFuzzEntry, std::mutex& mtx_backwardAnalysis, std::map<int, ToggleData> &ToggleBasic, std::mutex& mtx_ToggleBasic)
{
    for (auto ii = subOptimizedTogglePoints.begin(); ii != subOptimizedTogglePoints.end(); ii++)
    {
        ParallelBackwardAnalysis(*ii, withinSameBB, total_reachable, b, mtx_global_bIndex, ToggleInfo, myfile, allBasicBlocks, callgraph, mainFuzzEntry, mtx_backwardAnalysis, ToggleBasic, mtx_ToggleBasic);
    }
}

void ParallelBackwardAnalysis(const ICFGNode* target, std::set<const BasicBlock*>& withinSameBB, int & total_reachable, int & b, std::mutex& mtx_global_bIndex, ostream &ToggleInfo, ostream &myfile, std::set<const llvm::BasicBlock *> &allBasicBlocks, 
                                SVF::PTACallGraph *callgraph, const llvm::Function *mainFuzzEntry, std::mutex& mtx_backwardAnalysis, std::map<int, ToggleData> &ToggleBasic, std::mutex& mtx_ToggleBasic)
{

    /* Conduct Backward Reachability to each toggle Target */
    std::set<const Function*> RustStdLibFunc;
    std::set<const ICFGNode*> checked_iNodes;
    int reachable = 0;
    std::set<const BasicBlock*> ToggleBB;
    std::set<const BasicBlock*> ToggleSTDBB;


    const SVFFunction* svfFun = target->getFun();
    if(svfFun) {
        const Function* func = target->getFun()->getLLVMFun();
        if(isDroplisted(func)) return;
        // if(isBlacklisted(func) && !isCMLibrarylisted(func)) return;
        


        for (auto &BB : *func) {
            if(allBasicBlocks.find(&BB)!= allBasicBlocks.end()) { 
                std::lock_guard<std::mutex> lock(mtx_backwardAnalysis);
                if(ToggleBB.find(&BB) == ToggleBB.end()) {
                    // reachable++;
                    ToggleBB.insert(&BB);
                }
            }
        }

        
    }

    // /* If the toggle point is in the same basic block, skip analyzing */
    const BasicBlock* targetBB = target->getBB();
    if(targetBB) {
        std::lock_guard<std::mutex> lock(mtx_backwardAnalysis);
        if(withinSameBB.find(targetBB) != withinSameBB.end()) return;
        withinSameBB.insert(targetBB);
    }

    /* If debug_pruning_rate is true, we need to compute backward reachability basic block ratio with std library basic block (S2FuzzBackwardAnalysis_with_pruning_info)
        Otherwise, it just conducts backward reachability analysis without actually compute basic block ratio (S2FuzzBackwardAnalysis)
    */

    
    checked_iNodes = S2FuzzBackwardAnalysis(target, callgraph, mainFuzzEntry, RustStdLibFunc);

    /* If we cannot find any reachable path from target points, we skip further analysis */
    if(checked_iNodes.empty()) return;
    
    {
        std::lock_guard<std::mutex> lock(mtx_backwardAnalysis);

        std::string lineInfoStr;
        /* Get Line Info */
        if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(target))
        {
            Instruction* I = const_cast<Instruction*>(intra->getInst());
            if (DILocation *Loc = I->getDebugLoc()) { 
                unsigned Line = Loc->getLine();
                StringRef File = Loc->getFilename();
                StringRef Directory = Loc->getDirectory();
                
                lineInfoStr = " (File: " + Directory.str() + "/" + File.str() + ", Line: " + std::to_string(Line) + ")";
            }  
            else {
                lineInfoStr = " (" + std::string("File: ") + "Unknown" + ", Line: " + "Unknown)";
            }  
        }
        else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(target))
        {
            Instruction* I = const_cast<Instruction*>(callNode->getCallSite());
            if (DILocation *Loc = I->getDebugLoc()) { 
                unsigned Line = Loc->getLine();
                StringRef File = Loc->getFilename();
                StringRef Directory = Loc->getDirectory();
            
                lineInfoStr = " (File: " + Directory.str() + "/" + File.str() + ", Line: " + std::to_string(Line) + ")";
            } 
            else {
                lineInfoStr = " (" + std::string("File: ") + "Unknown" + ", Line: " + "Unknown)";
            }
        }
        else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(target))
        {
            Instruction* I = const_cast<Instruction*>(RetNode->getCallSite());
            if (DILocation *Loc = I->getDebugLoc()) { 
                unsigned Line = Loc->getLine();
                StringRef File = Loc->getFilename();
                StringRef Directory = Loc->getDirectory();
                
                lineInfoStr = std::string("File: ") + Directory.str() + "/" + File.str() + ", Line: " + std::to_string(Line);
            } 
            else {
                lineInfoStr = " (" + std::string("File: ") + "Unknown" + ", Line: " + "Unknown)";
            }
        }
        else {
            lineInfoStr = " (" + std::string("File: ") + "Unknown" + ", Line: " + "Unknown)";
        }

        {
            std::lock_guard<std::mutex> lock(mtx_ToggleBasic);
            ToggleBasic[b].lineInfo = lineInfoStr;
            ToggleInfo << "[" << b <<"] : ";
        }
        
        // if(ToggleBasic[b].lineInfo == " (" + std::string("File: ") + "Unknown" + ", Line: " + "Unknown)") continue;
        /* Get Line Info */

        

        for(auto ci = checked_iNodes.begin(); ci != checked_iNodes.end(); ci++){
            const BasicBlock* bb = (*ci)->getBB();            

            /* Write Reachable Basic Block Unique ID */
            if(bb) {
                // if(allBasicBlocks.find(bb)!= allBasicBlocks.end()) {
                    if(ToggleBB.find(bb) == ToggleBB.end()) {
                        reachable++;
                        ToggleBB.insert(bb);
                    }

                // }
            }

        }

        for(auto fi = RustStdLibFunc.begin(); fi!= RustStdLibFunc.end(); fi++) {
            const Function* func = *fi;
            for (auto &BB : *func) {
                if(allBasicBlocks.find(&BB)!= allBasicBlocks.end()) { 
                    if(ToggleSTDBB.find(&BB) == ToggleSTDBB.end()) {
                        reachable++;
                        ToggleSTDBB.insert(&BB);
                    }
                }
                
            }

        }


        myfile << "[Toggle Point " << b <<"] : " << target->toString() << "\n";
        myfile << "Reachable BB : " << reachable << "\n\n";
        /* Debugging -> No Actual Git Push */
    
        for(auto &bb : ToggleBB) {

            for (auto &I : *bb) {
                if (auto *CI = dyn_cast<CallInst>(&I)) {
                    if(MDNode *MD = CI->getMetadata("bb_index")) {
                        if (llvm::Metadata* md = MD->getOperand(0)) {
                            if (auto* mdv = llvm::dyn_cast<llvm::ConstantAsMetadata>(md)) {
                                if (llvm::ConstantInt* ci = llvm::dyn_cast<llvm::ConstantInt>(mdv->getValue())) {
                                    uint64_t bbIndexValue = ci->getZExtValue();
                                    ToggleInfo << bbIndexValue << " ";
                                    {
                                        std::lock_guard<std::mutex> lock(mtx_ToggleBasic);
                                        ToggleBasic[b].BBIds.insert(bbIndexValue);
                                    }

                                }

                            }
                        }
                    }
                }
            }

            // /* Debugging -> No Actual Git Push */
            // for(auto ti = ToggleBB.begin(); ti != ToggleBB.end(); ti++) {
            //     const BasicBlock* bb = *ti;
            //     if(bb) {
            //         myfile << "Func : " << bb->getParent()->getName().str() << " BB : " << bb->getName().str() << "\n";
            //     }
                
            // }
            // /* Debugging -> No Actual Git Push */


        }

        ToggleInfo << "\n";   
        total_reachable+= reachable; 

        {
            std::lock_guard<std::mutex> lock(mtx_global_bIndex);
            b++;        
        }

    
    }


        
}



void TrackRawPtrSites(const Instruction* inst, const SVFG* svfg)
{
    const SVFGNode* sNode = *((svfg->fromValue(inst)).begin());
    FIFOWorkList<const SVFGNode*> worklist;
    std::set<const SVFGEdge*> visited;
    std::set<const SVFGNode*> nvisited;
    worklist.push(sNode);

    while (!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        for (SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd(); 
            it != eit; ++it)
        {
            SVFGEdge* edge = *it;
            SVFGNode* nextNode = edge->getDstNode();
            if(visited.find(edge) == visited.end())
            {
                s32_t nKind = nextNode->getNodeKind();
                if(nKind == VFGNode::Store || nKind == VFGNode::Load)
                {
                    if(nvisited.find(nextNode) == nvisited.end())
                    {
                        const ICFGNode* iNode = nextNode->getICFGNode();
                        if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(iNode))
                        {
                            Instruction* inst = const_cast<Instruction*>(intra->getInst());
                            LLVMContext &context = inst->getContext();
                            MDString *str = MDString::get(context, "rawptrAccess");
                            MDNode *node = MDNode::get(context, str);
                            inst->setMetadata("rawptrAccess", node);
                        }
                        else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode))
                        {
                            Instruction* inst = const_cast<Instruction*>(callNode->getCallSite());
                            LLVMContext &context = inst->getContext();
                            MDString *str = MDString::get(context, "rawptrAccess");
                            MDNode *node = MDNode::get(context, str);
                            inst->setMetadata("rawptrAccess", node);
                        }
                        else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(iNode))
                        {
                            Instruction* inst = const_cast<Instruction*>(RetNode->getCallSite());
                            LLVMContext &context = inst->getContext();
                            MDString *str = MDString::get(context, "rawptrAccess");
                            MDNode *node = MDNode::get(context, str);
                            inst->setMetadata("rawptrAccess", node);
                        }

                        nvisited.insert(nextNode);

                    }

                }
                worklist.push(nextNode);
                visited.insert(edge);

            }

        }
    }
    

}

void TrackAccessSitesAfterReturn(const SVFGNode* sNode, std::set<const SVFGEdge*> &visited)
{
    FIFOWorkList<const SVFGNode*> worklist;
    worklist.push(sNode);
    
    while(!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
            it != eit; ++it)
        {
            SVFGEdge* edge = *it;
            SVFGNode* nextNode = edge->getDstNode();
            if(visited.find(edge) == visited.end()) {
                s32_t nKind = nextNode->getNodeKind();
                if(nKind == VFGNode::Store || nKind == VFGNode::Load)
                {
                    const ICFGNode* iNode = nextNode->getICFGNode();
                    if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(iNode))
                    {
                        Instruction* inst = const_cast<Instruction*>(intra->getInst());
                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "StackafterReturnAccess");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("StackafterReturnAccess", node);
                    }
                    else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode))
                    {
                        Instruction* inst = const_cast<Instruction*>(callNode->getCallSite());
                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "StackafterReturnAccess");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("StackafterReturnAccess", node);
                    }
                    else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(iNode))
                    {
                        Instruction* inst = const_cast<Instruction*>(RetNode->getCallSite());
                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "StackafterReturnAccess");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("StackafterReturnAccess", node);
                    }

                }
                worklist.push(nextNode);
                visited.insert(edge);
            }


        }


    }

}


void TrackAccessSitesAfterDrop(const SVFGNode* sNode, std::set<const SVFGEdge*> &visited)
{
    FIFOWorkList<const SVFGNode*> worklist;
    worklist.push(sNode);
    
    while(!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
            it != eit; ++it)
        {
            SVFGEdge* edge = *it;
            SVFGNode* nextNode = edge->getDstNode();
            if(visited.find(edge) == visited.end()) {
                s32_t nKind = nextNode->getNodeKind();
                if(nKind == VFGNode::Store || nKind == VFGNode::Load)
                {
                    const ICFGNode* iNode = nextNode->getICFGNode();
                    if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(iNode))
                    {
                        Instruction* inst = const_cast<Instruction*>(intra->getInst());
                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "HeapafterDropAccess");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("HeapafterDropAccess", node);
                    }
                    else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode))
                    {
                        Instruction* inst = const_cast<Instruction*>(callNode->getCallSite());
                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "HeapafterDropAccess");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("HeapafterDropAccess", node);
                    }
                    else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(iNode))
                    {
                        Instruction* inst = const_cast<Instruction*>(RetNode->getCallSite());
                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "HeapafterDropAccess");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("HeapafterDropAccess", node);
                    }

                }
                worklist.push(nextNode);
                visited.insert(edge);
            }


        }


    }

}



void afterDropOpt(const SVFGNode* sNode, ofstream &myfile)
{
    FIFOWorkList<const SVFGNode*> worklist;
    std::set<const SVFGEdge*> visited;
    worklist.push(sNode);
    while(!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
            it != eit; ++it)
        {
            SVFGEdge* edge = *it;
            SVFGNode* nextNode = edge->getDstNode();
            if(visited.find(edge) == visited.end())
            {
                string dropstr = nextNode->toString();
                if(dropstr.find("drop_in_place") != std::string::npos) {
                    TrackAccessSitesAfterDrop(nextNode, visited);
                }
                visited.insert(edge);
                worklist.push(nextNode);
            }

        }

    }

}

void MarkHeapAllocationSites(const SVFGNode* sNode)
{
    const ICFGNode* iNode = sNode->getICFGNode();
    if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(iNode))
    {
        Instruction* inst = const_cast<Instruction*>(intra->getInst());
        LLVMContext &context = inst->getContext();
        MDString *str = MDString::get(context, "heap_allocation_site");
        MDNode *node = MDNode::get(context, str);
        inst->setMetadata("heap_allocation_site", node);
    }

    else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode))
    {
        Instruction* inst = const_cast<Instruction*>(callNode->getCallSite());
        LLVMContext &context = inst->getContext();
        MDString *str = MDString::get(context, "heap_allocation_site");
        MDNode *node = MDNode::get(context, str);
        inst->setMetadata("heap_allocation_site", node);
    }

    else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(iNode))
    {
        Instruction* inst = const_cast<Instruction*>(RetNode->getCallSite());
        LLVMContext &context = inst->getContext();
        MDString *str = MDString::get(context, "heap_allocation_site");
        MDNode *node = MDNode::get(context, str);
        inst->setMetadata("heap_allocation_site", node);
    }

}

void MarkHeapAccessSites(const SVFGNode* sNode, std::set<const SVFGEdge*>& visited)
{
    FIFOWorkList<const SVFGNode*> worklist;
    worklist.push(sNode);
    while(!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
            it != eit; ++it)
            {
                SVFGEdge* edge = *it;
                SVFGNode* nextNode = edge->getDstNode();
                if(visited.find(edge) == visited.end())
                {
                    // s32_t nKind = nextNode->getNodeKind();
                    // if(nKind == VFGNode::Store || nKind == VFGNode::Load)
                    // {
                    //     // Heap Sites Access
                    //     const ICFGNode* iNode = nextNode->getICFGNode();
                    //     if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(iNode))
                    //     {
                    //         Instruction* inst = const_cast<Instruction*>(intra->getInst());
                    //         LLVMContext &context = inst->getContext();
                    //         MDString *str = MDString::get(context, "heap_access_site");
                    //         MDNode *node = MDNode::get(context, str);
                    //         inst->setMetadata("heap_access_site", node);
                    //     }

                    //     else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode))
                    //     {
                    //         Instruction* inst = const_cast<Instruction*>(callNode->getCallSite());
                    //         LLVMContext &context = inst->getContext();
                    //         MDString *str = MDString::get(context, "heap_access_site");
                    //         MDNode *node = MDNode::get(context, str);
                    //         inst->setMetadata("heap_access_site", node);
                    //     }

                    //     else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(iNode))
                    //     {
                    //         Instruction* inst = const_cast<Instruction*>(RetNode->getCallSite());
                    //         LLVMContext &context = inst->getContext();
                    //         MDString *str = MDString::get(context, "heap_access_site");
                    //         MDNode *node = MDNode::get(context, str);
                    //         inst->setMetadata("heap_access_site", node);
                    //     }
                    // }

                    string dropstr = nextNode->toString();
                    if(dropstr.find("drop_in_place") != std::string::npos) {
                        TrackAccessSitesAfterDrop(nextNode, visited);
                    }


                    worklist.push(nextNode);
                    visited.insert(edge);
                }
            }
    }

}

void MarkStackAllocationSites(const SVFGNode* sNode)
{
    const ICFGNode* iNode = sNode->getICFGNode();
    if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(iNode))
    {
        Instruction* inst = const_cast<Instruction*>(intra->getInst());
        LLVMContext &context = inst->getContext();
        MDString *str = MDString::get(context, "stack_allocation_site");
        MDNode *node = MDNode::get(context, str);
        inst->setMetadata("stack_allocation_site", node);
    }

    else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode))
    {
        Instruction* inst = const_cast<Instruction*>(callNode->getCallSite());
        LLVMContext &context = inst->getContext();
        MDString *str = MDString::get(context, "stack_allocation_site");
        MDNode *node = MDNode::get(context, str);
        inst->setMetadata("stack_allocation_site", node);
    }

    else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(iNode))
    {
        Instruction* inst = const_cast<Instruction*>(RetNode->getCallSite());
        LLVMContext &context = inst->getContext();
        MDString *str = MDString::get(context, "stack_allocation_site");
        MDNode *node = MDNode::get(context, str);
        inst->setMetadata("stack_allocation_site", node);
    }
}

void MarkStackAccessSites(const SVFGNode* sNode, std::set<const SVFGEdge*>& visited)
{
    FIFOWorkList<const SVFGNode*> worklist;
    worklist.push(sNode);
    const SVFFunction* stackFun = sNode->getFun();
    while(!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
            it != eit; ++it)
            {
                SVFGEdge* edge = *it;
                SVFGNode* nextNode = edge->getDstNode();
                if(visited.find(edge) == visited.end())
                {
                    if(const SVF::FormalRetSVFGNode* retNode = dyn_cast<SVF::FormalRetSVFGNode>(nextNode))
                    {
                        const SVFFunction* retFun = retNode->getFun();
                        if(retFun == stackFun)
                        {
                            // From here we track stack access site after return scope
                            TrackAccessSitesAfterReturn(nextNode, visited);
                        }
                    }
                    // s32_t nKind = nextNode->getNodeKind();
                    // if(nKind == VFGNode::Store || nKind == VFGNode::Load)
                    // {
                    //     // Stack Sites Access
                    //     const ICFGNode* iNode = nextNode->getICFGNode();
                    //     if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(iNode))
                    //     {
                    //         Instruction* inst = const_cast<Instruction*>(intra->getInst());
                    //         LLVMContext &context = inst->getContext();
                    //         MDString *str = MDString::get(context, "stack_access_site");
                    //         MDNode *node = MDNode::get(context, str);
                    //         inst->setMetadata("stack_access_site", node);
                    //     }

                    //     else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode))
                    //     {
                    //         Instruction* inst = const_cast<Instruction*>(callNode->getCallSite());
                    //         LLVMContext &context = inst->getContext();
                    //         MDString *str = MDString::get(context, "stack_access_site");
                    //         MDNode *node = MDNode::get(context, str);
                    //         inst->setMetadata("stack_access_site", node);
                    //     }

                    //     else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(iNode))
                    //     {
                    //         Instruction* inst = const_cast<Instruction*>(RetNode->getCallSite());
                    //         LLVMContext &context = inst->getContext();
                    //         MDString *str = MDString::get(context, "stack_access_site");
                    //         MDNode *node = MDNode::get(context, str);
                    //         inst->setMetadata("stack_access_site", node);
                    //     }
                    // }
                    worklist.push(nextNode);
                    visited.insert(edge);
                }
            }
    }
}


void SafeReferenceOptimization(std::set<const SVFGNode*> &heapObjs, ofstream &myfile)
{
    for(auto i = heapObjs.begin(); i != heapObjs.end(); i++)
    {
        const SVFGNode* heapObj = *i;
        afterDropOpt(heapObj, myfile);
    }

}

void setMetadata(std::set<const SVFGNode*>& instructionSets, bool isStack, ofstream &myfile)
{
    if(isStack) {
        for(auto i = instructionSets.begin(); i != instructionSets.end(); i++)
        {
            const SVFGNode* sNode = *i;
            const ICFGNode* iNode = sNode->getICFGNode();
            

            if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(iNode))
            {
                Instruction* inst = const_cast<Instruction*>(intra->getInst());
                LLVMContext &context = inst->getContext();
                MDString *str = MDString::get(context, "stack_target");
                MDNode *node = MDNode::get(context, str);
                inst->setMetadata("stack_target", node);
            }

            else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode))
            {
                Instruction* inst = const_cast<Instruction*>(callNode->getCallSite());
                LLVMContext &context = inst->getContext();
                MDString *str = MDString::get(context, "stack_target");
                MDNode *node = MDNode::get(context, str);
                inst->setMetadata("stack_target", node);
            }

            else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(iNode))
            {
                Instruction* inst = const_cast<Instruction*>(RetNode->getCallSite());
                LLVMContext &context = inst->getContext();
                MDString *str = MDString::get(context, "stack_target");
                MDNode *node = MDNode::get(context, str);
                inst->setMetadata("stack_target", node);
            }

        }
    }


    else {
        for(auto i = instructionSets.begin(); i != instructionSets.end(); i++)
        {
            const SVFGNode* sNode = *i;
            const ICFGNode* iNode = sNode->getICFGNode();
            

            if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(iNode))
            {
                Instruction* inst = const_cast<Instruction*>(intra->getInst());
                LLVMContext &context = inst->getContext();
                MDString *str = MDString::get(context, "heap_target");
                MDNode *node = MDNode::get(context, str);
                inst->setMetadata("heap_target", node);
            }

            else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode))
            {
                Instruction* inst = const_cast<Instruction*>(callNode->getCallSite());
                LLVMContext &context = inst->getContext();
                MDString *str = MDString::get(context, "heap_target");
                MDNode *node = MDNode::get(context, str);
                inst->setMetadata("heap_target", node);
            }

            else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(iNode))
            {
                Instruction* inst = const_cast<Instruction*>(RetNode->getCallSite());
                LLVMContext &context = inst->getContext();
                MDString *str = MDString::get(context, "heap_target");
                MDNode *node = MDNode::get(context, str);
                inst->setMetadata("heap_target", node);
            }

        }
    }

}


void ErasanAnalysis_opt(SVFModule* svfModule, Module* module, const SVFG* svfg, ICFG* icfg, PTACallGraph* callgraph)
{
    ofstream myfile;
    myfile.open("Erasan_opt.txt", ios::out | ios::trunc);
    std::set<const SVFGNode*> topObjs = SanitizerRawPtrInstructions(module, svfg, callgraph);
    
    /* Raw Pointer Related Heap Objects */
    std::set<const SVFGNode*> heapObjs;
    std::set<const SVFGNode*> heapRelatedAllInst;
    std::set<const SVFGEdge*> heapvisited;

    /* Raw Pointer Related Stack Objects */
    std::set<const SVFGNode*> stackObjs;
    std::set<const SVFGNode*> stackRelatedAllInst;
    std::set<const SVFGEdge*> stackvisited;

    /* Counting Stack/Heap Object related to the Raw Pointers */
    int a = 0;

    for(auto ii = topObjs.begin(); ii != topObjs.end(); ii++)
    {
        a++;
    }

    std::cout << "\n******" << "Total Allocation Sites : " << a << "******\n";
    
    std::set<const SVFGNode*> heapNodes;
    std::set<const SVFGNode*> stackNodes;

    for(auto ii = topObjs.begin(); ii != topObjs.end(); ii++)
    {
        const SVFGNode* topPtr = *ii;  
        // myfile << "topObj node : " << topPtr->toString() << "\n\n"; 
        HeapOrStackIdentification(topPtr, heapObjs, stackObjs, heapNodes, stackNodes);
        
    }

    std::cout << "\n ******Finished Identifying HeapOrStack Allocation Sites******\n";
    
    a = 0;
    for(auto hi = heapObjs.begin(); hi != heapObjs.end(); hi++)
    {
        const SVFGNode* heapObj = *hi;
        MarkHeapAllocationSites(heapObj);
        MarkHeapAccessSites(heapObj, heapvisited);
        a++;
    }

    std::cout << "\n ******Finished Marking Heap Access Sites******\n";
    for(auto si = stackObjs.begin(); si != stackObjs.end(); si++)
    {
        const SVFGNode* stackObj = *si;
        MarkStackAllocationSites(stackObj);
        MarkStackAccessSites(stackObj, stackvisited);
        a++;
    }
    std::cout << "\n ******Finished Marking Stack Access Sites******\n";
    // myfile << "Stack Alloca Sites : " << a << "\n";



    // /* Track All Stack-Related Instructions related to the Raw Pointers */
    // for(auto si = stackObjs.begin(); si != stackObjs.end(); si++)
    // {
    //     const SVFGNode* stackObj = *si;
    //     if(stackRelatedAllInst.find(stackObj) == stackRelatedAllInst.end()) {
    //         stackRelatedAllInst.insert(stackObj);
    //     }
    //     TrackAccessSites(stackObj, stackRelatedAllInst, stackvisited, myfile);
    // }

    // /* Track All Heap-Related Instructions related to the Raw Pointers */
    // for(auto hi = heapObjs.begin(); hi != heapObjs.end(); hi++)
    // {
    //     const SVFGNode* heapObj = *hi;
    //     if(heapRelatedAllInst.find(heapObj) == heapRelatedAllInst.end()) {
    //         heapRelatedAllInst.insert(heapObj);
    //     }
    //     TrackAccessSites(heapObj, heapRelatedAllInst, heapvisited, myfile);
    // }

    // /* Set Target Metadata for Stack-Related (stack_target) Instructions */
    // setMetadata(stackRelatedAllInst, true, myfile);

    // /* Set Target Metadata for Heap-Related (heap_target) Instructions */
    // setMetadata(heapRelatedAllInst, false, myfile);

    /* Optimize for safe reference (memory access after drop)*/
    // SafeReferenceOptimization(heapObjs, myfile);

    myfile.close();
    writeModuleToFile(module, "erasanOptAnalysis.ll");


}

/* ---------------------------------------------------------------------------------------------------------------------------------- */
/* Rust Sanitizer Static Analysis - Thread Safe Analysis */

void setMetatdataToThreadFunc(Function *F) {

    llvm::LLVMContext &Context = F->getContext();
    MDString *str = MDString::get(Context, "ThreadFuncAccess");
    MDNode *node = MDNode::get(Context, str);


    for (auto &BB : *F) {
        for(auto &I : BB) {
            if (llvm::isa<llvm::LoadInst>(&I)) {
                I.setMetadata("ThreadFuncAccess", node);
            } 
            
            else if (llvm::isa<llvm::StoreInst>(&I)) {
                I.setMetadata("ThreadFuncAccess", node);
            }
        }
    }

}


void ThreadSafeMarkStackAccessSites(const SVFGNode* sNode, std::set<const SVFGEdge*>& visited)
{
    FIFOWorkList<const SVFGNode*> worklist;
    worklist.push(sNode);
    const SVFFunction* stackFun = sNode->getFun();
    while(!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
            it != eit; ++it)
            {
                SVFGEdge* edge = *it;
                SVFGNode* nextNode = edge->getDstNode();
                if(visited.find(edge) == visited.end())
                {
                    const SVFFunction* svfFun = nextNode->getFun();
                    if(svfFun) {
                        Function* threadCandidate = svfFun->getLLVMFun();
                        if(isThreadlisted(threadCandidate)) {
                            setMetatdataToThreadFunc(threadCandidate);
                        }
                    }

                    if(const SVF::FormalRetSVFGNode* retNode = dyn_cast<SVF::FormalRetSVFGNode>(nextNode))
                    {
                        const SVFFunction* retFun = retNode->getFun();
                        if(retFun == stackFun)
                        {
                            // From here we track stack access site after return scope
                            TrackAccessSitesAfterReturn(nextNode, visited);
                        }
                    }
             
                    worklist.push(nextNode);
                    visited.insert(edge);
                }
            }
    }
}


void ThreadSafeMarkHeapAccessSites(const SVFGNode* sNode, std::set<const SVFGEdge*>& visited)
{
    FIFOWorkList<const SVFGNode*> worklist;
    worklist.push(sNode);
    while(!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
            it != eit; ++it)
            {
                SVFGEdge* edge = *it;
                SVFGNode* nextNode = edge->getDstNode();
                if(visited.find(edge) == visited.end())
                {
                    const SVFFunction* svfFun = nextNode->getFun();
                    if(svfFun) {
                        Function* threadCandidate = svfFun->getLLVMFun();
                        if(isThreadlisted(threadCandidate)) {
                            setMetatdataToThreadFunc(threadCandidate);
                        }
                    }

                    string dropstr = nextNode->toString();
                    if(dropstr.find("drop_in_place") != std::string::npos) {
                        TrackAccessSitesAfterDrop(nextNode, visited);
                    }


                    worklist.push(nextNode);
                    visited.insert(edge);
                }
            }
    }

}



void ErasanAnalysis_thread_safe(SVFModule* svfModule, Module* module, const SVFG* svfg, ICFG* icfg, PTACallGraph* callgraph)
{
    ofstream myfile;
    myfile.open("Erasan_thread_safe.txt", ios::out | ios::trunc);
    std::set<const SVFGNode*> topObjs = SanitizerRawPtrInstructions(module, svfg, callgraph);
    
    /* Raw Pointer Related Heap Objects */
    std::set<const SVFGNode*> heapObjs;
    std::set<const SVFGNode*> heapRelatedAllInst;
    std::set<const SVFGEdge*> heapvisited;

    /* Raw Pointer Related Stack Objects */
    std::set<const SVFGNode*> stackObjs;
    std::set<const SVFGNode*> stackRelatedAllInst;
    std::set<const SVFGEdge*> stackvisited;

    /* Counting Stack/Heap Object related to the Raw Pointers */
    int a = 0;

    for(auto ii = topObjs.begin(); ii != topObjs.end(); ii++)
    {
        a++;
    }

    std::cout << "\n******" << "Total Allocation Sites : " << a << "******\n";
    
    std::set<const SVFGNode*> heapNodes;
    std::set<const SVFGNode*> stackNodes;

    for(auto ii = topObjs.begin(); ii != topObjs.end(); ii++)
    {
        const SVFGNode* topPtr = *ii;  
        // myfile << "topObj node : " << topPtr->toString() << "\n\n"; 
        HeapOrStackIdentification(topPtr, heapObjs, stackObjs, heapNodes, stackNodes);
        
    }

    std::cout << "\n ******Finished Identifying HeapOrStack Allocation Sites******\n";
    
    a = 0;
    for(auto hi = heapObjs.begin(); hi != heapObjs.end(); hi++)
    {
        const SVFGNode* heapObj = *hi;
        MarkHeapAllocationSites(heapObj);
        ThreadSafeMarkHeapAccessSites(heapObj, heapvisited);
        a++;
    }

    std::cout << "\n ******Finished Marking Heap Access Sites******\n";
    for(auto si = stackObjs.begin(); si != stackObjs.end(); si++)
    {
        const SVFGNode* stackObj = *si;
        MarkStackAllocationSites(stackObj);
        ThreadSafeMarkStackAccessSites(stackObj, stackvisited);
        a++;
    }
    std::cout << "\n ******Finished Marking Stack Access Sites******\n";



    myfile.close();
    writeModuleToFile(module, "erasanThreadSafeAnalysis.ll");


}




/* ---------------------------------------------------------------------------------------------------------------------------------- */

/* Rust Sanitizer Static Analysis - Unsafe Region Reachabilty Analysis */
/* 
    (1) Unsafe Instructions Finding - SanitizerUnsafeInstructions
    (2) All Backward Reachability from Unsafe Regions - SanitizerUnsafeBackwardReachability
    (3) All Forward Reachability from Unsafe Regions - SanitizerUnsafeForwardReachability
    (4) Set metadata for Unsafe Region Instructions - setMetadatatoUnsafeRegions
*/

bool containsUnsafeBlock(const std::string& filename, const int lineNumber) {
    std::ifstream file(filename);
    if(!file.is_open()) {
        return false;
    }

    std::string line;
    int currentLine = 0;
    bool insideFunction = false;
    bool insideUnsafeBlock = false;
    std::stack<char> braceStack;

    while (std::getline(file, line)) {
        currentLine++;

        if (currentLine == lineNumber) {
            insideFunction = true;
        }

        if (!insideFunction) continue;

        // Check for opening and closing braces to track the scope
        for (char c : line) {
            if (c == '{') {
                braceStack.push('{');
            } else if (c == '}') {
                if (!braceStack.empty() && braceStack.top() == '{') {
                    braceStack.pop();
                    if (braceStack.empty()) {
                        insideFunction = false;  // We've exited the function
                        break;
                    }
                }
            }
        }

        // Check for the start of an unsafe block
        if (line.find("unsafe {") != std::string::npos && insideFunction) {
            insideUnsafeBlock = true;
        }

        // If we find an unsafe block within the function, return true
        if (insideUnsafeBlock && braceStack.size() == 1) { // Assuming unsafe block is not nested
            return true;
        }

        if (!insideFunction) {
            break;  // Exit the loop once the function ends
        }
    }

    file.close();
    return false;

}


void ExternalUnsafeCheck(Module* llvmModule) {

    std::string filename = "UnsafeInfo.txt";
    std::string labelname = "UnsafeLabelInfo.txt";
   
    std::ofstream outstream(filename, std::ios::app);
    std::ofstream outlabelstream (labelname, std::ios::app);
    if(!outstream.is_open() || !outlabelstream.is_open()) {
        return;
    }

    for (auto &function : *llvmModule) {
        if (llvm::DISubprogram *subprogram = function.getSubprogram()) {
            std::string labelName = function.getName().str();
            // std::cout << "Function: " << function.getName().str()<<"\n";
            std::string functionName = demangleString(function.getName().str().c_str());
            // std::cout << "Filename: " << subprogram->getFile()->getFilename().str() << "\n";
            std::string filename = subprogram->getFile()->getFilename().str();
            // std::cout << "Line: " << subprogram->getLine() << "\n";
            std::string lineNumberStr = std::to_string(subprogram->getLine());
            
            int lineNumber = std::stoi(lineNumberStr);

            if(containsUnsafeBlock(filename, lineNumber)) {
                size_t pos = labelName.rfind("17");
                if (pos != std::string::npos) {
                    labelName = labelName.substr(0, pos + 2);
                }
                outlabelstream << labelName << "\n";
                
                // std::cout << "Function has unsafe block : " << functionName << " has an unsafe block\n";
                // std::cout << "Filename : " << filename << "\n";
                // std::cout << "Line : " << lineNumber << "\n";

                // llvm::LLVMContext &context = llvmModule->getContext();
                // llvm::MDNode *unsafeMetadata = llvm::MDNode::get(context, llvm::MDString::get(context, "contains unsafe block"));
                // function.setMetadata("unsafe_block", unsafeMetadata);
                size_t lastColon = functionName.rfind("::");
                if (lastColon != std::string::npos) {
                    functionName = functionName.substr(0, lastColon + 2); // +2 to include the "::"
                }
                outstream << functionName << "\n";
                RustUnsafeFuncsMark(function);
                // outstream << demangleString(functionName.c_str()) << "\n";
            }


        }
    }
     outstream.close();
     outlabelstream.close();
}

void UnsafePreprocess(Module* llvmModule) {
    // Need to call ExternalUnsafeCheck function first to set metadata for external unsafe blocks
    ExternalUnsafeCheck(llvmModule);
    writeModuleToFile(llvmModule, "RustGoUnsafeMarkedIR.ll");
}

std::set<const ICFGNode*> SanitizerUnsafeInstructions(Module* llvmModule, const SVFG* svfg, PTACallGraph* callgraph) 
{
    std::set<const ICFGNode*> unsafeInst;


    /* ###################Working with UnsafePreprocess function################### */
    std::set<std::string> unsafeFunctionNames;
    std::set<std::string> unsafeLabelNames;
    std::string filename = "UnsafeInfo.txt";
    std::string labelname = "UnsafeLabelInfo.txt";
    std::ifstream infile(filename);
    std::ifstream labelfile(labelname);
    std::string line;
    std::string label_line;


    if (!infile.is_open() || !labelfile.is_open()) {
        std::cerr << "Failed to open " << filename << std::endl;
        return unsafeInst; // Return the empty set if the file can't be opened
    }

    while(std::getline(infile, line)) {
        unsafeFunctionNames.insert(line);
    }
    infile.close();

    while(std::getline(labelfile, label_line)) {
        unsafeLabelNames.insert(label_line);
    }
    labelfile.close();

    /* ###################Working with UnsafePreprocess function################### */
    
    for (auto &function : *llvmModule) {
        
        /* ###################Working with UnsafePreprocess function################### */
        std::string functionName = demangleString(function.getName().str().c_str());
        size_t lastColon = functionName.rfind("::");
        if (lastColon != std::string::npos) {
            functionName = functionName.substr(0, lastColon + 2); // +2 to include the "::"
        }

        // std::cout << "Function: " << functionName << "\n";


        if(unsafeFunctionNames.find(functionName)!= unsafeFunctionNames.end()) {
            std::string label = function.getName().str();
            // std::cout << "Function: " << functionName << "\n";
            llvm::BasicBlock &firstBB = function.front();
            llvm::Instruction &firstInst = firstBB.front();
            // std::cout << "Unsafe Block Start: " << firstBB.getName().str() << "\n";
            // std::cout << "Unsafe Instruction Start: " << firstInst.getName().str() << "\n";
            const SVFGNode* sNode = *((svfg->fromValue(&firstInst)).begin());
            if(sNode) {
                const ICFGNode* iNode = sNode->getICFGNode();
                unsafeInst.insert(iNode);
                // std::cout << "Unsafe Snode Instruction: " << iNode->toString() << "\n";
            }

        }
        /* ###################Working with UnsafePreprocess function################### */

        for (auto &bb : function) {
            /* ###################Working with UnsafePreprocess function################### */
            std::string bbName = bb.getName().str();
            for(auto &label: unsafeLabelNames) {
                if(bbName.find(label)!= std::string::npos) {
                    for (auto &inst : bb) {
                        // std::cout << bbName << " " << inst.getName().str() << "\n";
                        const SVFGNode* sNode = *((svfg->fromValue(&inst)).begin());
                        if(sNode) {
                            const ICFGNode* iNode = sNode->getICFGNode();
                            unsafeInst.insert(iNode);
                        }
                    }
                }
            }
            /* ###################Working with UnsafePreprocess function################### */


            for (auto &inst : bb) {

                // Check whether the instruction within unsafe block
                if(inst.getMetadata("ydy.unsafe"))
                {
                    const SVFGNode* sNode = *((svfg->fromValue(&inst)).begin());
                    if(sNode) {
                        // std::cout <<  sNode->toString() << "\n";
                        const ICFGNode* iNode = sNode->getICFGNode();
                        unsafeInst.insert(iNode);
                    }

                }
            }
        }
    }

    return unsafeInst;

}



void backwardAnalysis_unsafe(const ICFGNode* iNode, std::set<const ICFGNode*>& checked_iNodes, std::set<ICFGEdge*>& ivisited, PTACallGraph* callgraph) 
{
    checked_iNodes.insert(iNode);
    FIFOWorkList<const ICFGNode*> worklist;
    const SVFFunction* targetFun = iNode->getFun();
    worklist.push(iNode);


    while(!worklist.empty())
    {
        const ICFGNode* vNode = worklist.pop();

        // if(SVFUtil::isa<CallBlockNode>(vNode)) {
        //     for (ICFGNode::const_iterator it = vNode->OutEdgeBegin(), eit =
        //             vNode->OutEdgeEnd(); it != eit; ++it) {
        //         ICFGEdge* libEdge = *it;
        //         if(CallCFGEdge* dirCall = SVFUtil::dyn_cast<CallCFGEdge>(libEdge)) {
        //             ICFGNode* libNode = libEdge->getDstNode();
        //             // Validate that the dest Node has no outgoing edge
        //             if(!libNode->hasOutgoingEdge()) {
        //                 checked_iNodes.insert(libNode);
        //             }
        //         }
        //     }
        // }

        for(ICFGNode::const_iterator it = vNode->InEdgeBegin(), eit =
                vNode->InEdgeEnd(); it != eit; ++it) {
            ICFGEdge* edge = *it;
            ICFGNode* prevNode = edge->getSrcNode();
            // const SVFFunction* prevFun = prevNode->getFun();

            if(ivisited.find(edge) == ivisited.end()) {
                worklist.push(prevNode);
                checked_iNodes.insert(prevNode);
                // if(CallCFGEdge* dirCall = SVFUtil::dyn_cast<CallCFGEdge>(edge)) {
                    
                //     if(isReachableForRust(prevFun, targetFun, callgraph)) {
                //         if((targetFun!=nullptr) && (prevFun!=nullptr)) {
                //             if(!isBlacklisted(prevFun->getLLVMFun())) {
                //                 checked_iNodes.insert(prevNode);
                //                 worklist.push(prevNode);                            
                //             }

                            
                //         }

                        
                //     }
                    
                // }


                // else if (RetCFGEdge* retCall = SVFUtil::dyn_cast<RetCFGEdge>(edge)) {
                //     if((targetFun!=nullptr) && (prevFun!=nullptr)) {
                //         const Function* candidate = prevFun->getLLVMFun();
                //         if(!isBlacklisted(candidate)) {
                //             worklist.push(prevNode);
                //             checked_iNodes.insert(prevNode);
                //         }

                //     }
                // }

                // else {
                //     worklist.push(prevNode);
                //     checked_iNodes.insert(prevNode);
                // }

                ivisited.insert(edge);


            }



        }

    }

}

void forwardAnalysis_unsafe(const ICFGNode* iNode, std::set<const ICFGNode*>& checked_iNodes, std::set<ICFGEdge*>& ivisited, PTACallGraph* callgraph) 
{
    FIFOWorkList<const ICFGNode*> worklist;
    worklist.push(iNode);

    while(!worklist.empty())
    {
        const ICFGNode* vNode = worklist.pop();
        for(ICFGNode::const_iterator it = vNode->OutEdgeBegin(), eit =
                vNode->OutEdgeEnd(); it != eit; ++it) {
            ICFGEdge* edge = *it;
            ICFGNode* nextNode = edge->getDstNode();
            if(ivisited.find(edge) == ivisited.end()) {
                worklist.push(nextNode);
                ivisited.insert(edge);
                checked_iNodes.insert(nextNode);
            }

        }
    }

}







void ErasanAnalysis_unsafe_reachabilty(SVFModule* svfModule, Module* module, const SVFG* svfg, ICFG* icfg, PTACallGraph* callgraph) 
{
    // (1) Get all ICFGNode related to the Unsafe Regions
    std::set<const ICFGNode*> unsafeInst = SanitizerUnsafeInstructions(module, svfg, callgraph);

    std::set<ICFGEdge*> visited;
    std::set<const ICFGNode*> unsafeReachableNodes;

    std::cout << "\n ******Finished Tracking Unsafe Metadata******\n";

    int a = 0;
    for(auto ii = unsafeInst.begin(); ii!= unsafeInst.end(); ii++)
    {
        a++;
        const ICFGNode* unsafeNode = *ii;
        backwardAnalysis_unsafe(unsafeNode, unsafeReachableNodes, visited, callgraph);
        forwardAnalysis_unsafe(unsafeNode, unsafeReachableNodes, visited, callgraph);
    }

    std::cout << "Unsafe Reachability - Number of unsafe instructions: " << a << "\n";


    std::cout << "\n ******Finished Reachability Analysis to Unsafe Instructions******\n";

    for(auto ii = unsafeReachableNodes.begin(); ii!= unsafeReachableNodes.end(); ii++)
    {
        const ICFGNode* iNode = *ii;
        if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(iNode))
        {
            Instruction* inst = const_cast<Instruction*>(intra->getInst());
            LLVMContext &context = inst->getContext();
            MDString *str = MDString::get(context, "unsafe_reachable");
            MDNode *node = MDNode::get(context, str);
            inst->setMetadata("unsafe_reachable", node);
        }

        else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode))
        {
            Instruction* inst = const_cast<Instruction*>(callNode->getCallSite());
            LLVMContext &context = inst->getContext();
            MDString *str = MDString::get(context, "unsafe_reachable");
            MDNode *node = MDNode::get(context, str);
            inst->setMetadata("unsafe_reachable", node);
        }

        else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(iNode))
        {
            Instruction* inst = const_cast<Instruction*>(RetNode->getCallSite());
            LLVMContext &context = inst->getContext();
            MDString *str = MDString::get(context, "unsafe_reachable");
            MDNode *node = MDNode::get(context, str);
            inst->setMetadata("unsafe_reachable", node);
        }

    }

    std::cout << "\n ******Finished Marking Unsafe-Reachability Metadata******\n";
    writeModuleToFile(module, "erasanUnsafeReachable.ll");


}

std::set<const SVFGNode*> SanitizerUnsafeInstructions_OPT(Module* llvmModule, const SVFG* svfg, PTACallGraph* callgraph) 
{
    std::set<const SVFGNode*> unsafeTopObj;

    /* ###################Working with UnsafePreprocess function################### */
    std::set<std::string> unsafeFunctionNames;
    std::set<std::string> unsafeLabelNames;
    std::string filename = "UnsafeInfo.txt";
    std::string labelname = "UnsafeLabelInfo.txt";
    std::ifstream infile(filename);
    std::ifstream labelfile(labelname);
    std::string line;
    std::string label_line;
    /* ###################Working with UnsafePreprocess function################### */
    int a =0;

    std::set<const SVFGEdge*> visited;
    /* ###################Working with UnsafePreprocess function################### */
    if (!infile.is_open() || !labelfile.is_open()) {
        std::cerr << "Failed to open " << filename << std::endl;
        return unsafeTopObj; // Return the empty set if the file can't be opened
    }

    while(std::getline(infile, line)) {
        unsafeFunctionNames.insert(line);
    }
    infile.close();

    while(std::getline(labelfile, label_line)) {
        unsafeLabelNames.insert(label_line);
    }
    labelfile.close();
    /* ###################Working with UnsafePreprocess function################### */

    for (auto &function : *llvmModule) {
        /* ###################Working with UnsafePreprocess function################### */
        std::string functionName = demangleString(function.getName().str().c_str());
        size_t lastColon = functionName.rfind("::");
        if (lastColon != std::string::npos) {
            functionName = functionName.substr(0, lastColon + 2); // +2 to include the "::"
        }

        if(unsafeFunctionNames.find(functionName)!= unsafeFunctionNames.end()) {
            for(auto &bb : function) {
                for(auto &inst : bb) {
                    // std::cout << "Unsafe Function: " << functionName << " BB: " << bb.getName().str() << std::endl;
                    bool isUnsafePointer = false;
                    if(inst.getType()->isPointerTy()) 
                    {
                        isUnsafePointer = true;
                    }

                    for (unsigned i = 0; i < inst.getNumOperands(); ++i) {
                        llvm::Value *op = inst.getOperand(i);
                        if(op->getType()->isPointerTy()) {
                            isUnsafePointer = true;
                        }
                    }

                    if(isUnsafePointer){
                        // std::cout << "Unsafe Pointer: " << inst.getName().str() << std::endl;
                        std::set<const SVFGNode*> instructionSet = TrackAllocSites(&inst,visited, svfg, callgraph);
                        for(auto i = instructionSet.begin(); i != instructionSet.end(); i++)
                        {
                            const SVFGNode* topObj = *i;
                            if(unsafeTopObj.find(topObj) == unsafeTopObj.end())
                            {
                                unsafeTopObj.insert(topObj);
                            }
                        }
                    }

                }
            }
            
        }
        /* ###################Working with UnsafePreprocess function################### */


        
        for (auto &bb : function) {
            /* ###################Working with UnsafePreprocess function################### */
            std::string bbName = bb.getName().str();
            for(auto &label: unsafeLabelNames) {
                if(bbName.find(label)!= std::string::npos) {
                    for(auto &inst : bb) {
                        std::cout << bbName << " " << inst.getName().str() << "\n";
                        bool isUnsafePointer = false;
                        if(inst.getType()->isPointerTy())
                        {
                            isUnsafePointer = true;
                        }
                        for (unsigned i = 0; i < inst.getNumOperands(); ++i) {
                            llvm::Value *op = inst.getOperand(i);
                            if(op->getType()->isPointerTy()) {
                                isUnsafePointer = true;
                            }
                        }

                        if(isUnsafePointer){
                            // std::cout << "Unsafe Pointer: " << inst.getName().str() << std::endl;
                            std::set<const SVFGNode*> instructionSet = TrackAllocSites(&inst, visited, svfg, callgraph);
                            for(auto i = instructionSet.begin(); i != instructionSet.end(); i++)
                            {
                                const SVFGNode* topObj = *i;
                                if(unsafeTopObj.find(topObj) == unsafeTopObj.end())
                                {
                                    unsafeTopObj.insert(topObj);
                                }
                            }
                        }

                    }
                }
            }
            /* ###################Working with UnsafePreprocess function################### */


            for (auto &inst : bb) {

                // Check whether the instruction within unsafe block
                if(inst.getMetadata("ydy.unsafe"))
                {
                    a++;
                    /* ###################Working with UnsafePreprocess function################### */
                    std::cout << "Unsafe Function: " << functionName << " BB: " << bb.getName().str() << std::endl;
                    bool isUnsafePointer = false;
                    if(inst.getType()->isPointerTy()) 
                    {
                        isUnsafePointer = true;
                    }

                    for (unsigned i = 0; i < inst.getNumOperands(); ++i) {
                        llvm::Value *op = inst.getOperand(i);
                        if(op->getType()->isPointerTy()) {
                            isUnsafePointer = true;
                        }
                    }
                    
                    if(isUnsafePointer){
                        // std::cout << "Unsafe Pointer: " << inst.getName().str() << std::endl;
                        std::set<const SVFGNode*> instructionSet = TrackAllocSites(&inst, visited, svfg, callgraph);
                        for(auto i = instructionSet.begin(); i != instructionSet.end(); i++)
                        {
                            const SVFGNode* topObj = *i;
                            if(unsafeTopObj.find(topObj) == unsafeTopObj.end())
                            {
                                unsafeTopObj.insert(topObj);
                            }
                        }
                    }
                    /* ###################Working with UnsafePreprocess function################### */


                    // std::set<const SVFGNode*> instructionSet = TrackAllocSites(&inst, visited, svfg, callgraph);
                    // for(auto i = instructionSet.begin(); i != instructionSet.end(); i++)
                    // {
                    //     const SVFGNode* topObj = *i;
                    //     if(unsafeTopObj.find(topObj) == unsafeTopObj.end())
                    //     {
                    //         unsafeTopObj.insert(topObj);
                    //     }
                    // }

                }

            }
        }
    }

    std::cout << "Unsafe Opt unsafe instructions: " << a << std::endl;

    return unsafeTopObj;

}




/* Erasan Unsafe Analysis with object tracking and memory acccessing */ 
void ErasanAnalysis_unsafe_opt(SVFModule* svfModule, Module* module, const SVFG* svfg, ICFG* icfg, PTACallGraph* callgraph)
{
    ofstream myfile;
    myfile.open("ErasanUnsafeOpt.txt", ios::out | ios::app);
    // (1) Get all TopAllocationSites related to the Unsafe Regions
    std::set<const SVFGNode*> topObjs = SanitizerUnsafeInstructions_OPT(module, svfg, callgraph);
    std::set<const SVFGNode*> allRelatedInst;
    std::set<const SVFGEdge*> svisited;
    // int a = 0;

    std::cout << "\n ******Finished Tracking Allocation Sites******\n";


    int a = 0;
    for(auto ii = topObjs.begin(); ii != topObjs.end(); ii++)
    {
        a++;
        const SVFGNode* topPtr = *ii;
        // a++;
        // myfile << "[" << a << "] topLevel Obj : " << topPtr->toString() << "\n\n";
        if(allRelatedInst.find(topPtr) == allRelatedInst.end())
        {
            allRelatedInst.insert(topPtr);
        }
        // SanitizerForwardAnalysis(topPtr, allRelatedInst, svisited, myfile);
        TrackAccessSites(topPtr, allRelatedInst, svisited, myfile);

    }

    std::cout << "\n ******Allocation Sites : " << a << "******\n";

    std::cout << "\n ******Finished Tracking Access Sites******\n";

    for(auto i = allRelatedInst.begin(); i != allRelatedInst.end(); i++)
    {
        const SVFGNode* sNode = *i;
        const ICFGNode* iNode = sNode->getICFGNode();
        

        if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(iNode))
        {
            Instruction* inst = const_cast<Instruction*>(intra->getInst());
            LLVMContext &context = inst->getContext();
            MDString *str = MDString::get(context, "unsafe_opt");
            MDNode *node = MDNode::get(context, str);
            inst->setMetadata("unsafe_opt", node);
        }
        else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode))
        {
            Instruction* inst = const_cast<Instruction*>(callNode->getCallSite());
            LLVMContext &context = inst->getContext();
            MDString *str = MDString::get(context, "unsafe_opt");
            MDNode *node = MDNode::get(context, str);
            inst->setMetadata("unsafe_opt", node);
        }
        else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(iNode))
        {
            Instruction* inst = const_cast<Instruction*>(RetNode->getCallSite());
            LLVMContext &context = inst->getContext();
            MDString *str = MDString::get(context, "unsafe_opt");
            MDNode *node = MDNode::get(context, str);
            inst->setMetadata("unsafe_opt", node);
        }


    }

    std::cout << "\n ******Finished Marking Unsafe-OPT Metadata******\n";
    myfile.close();
    writeModuleToFile(module, "erasanUnsafeOpt.ll");
}



/* ############################################################################################ */

/* S2Fuzz Implementation */
void S2FuzzTrackAccessSitesAfterReturn(const SVFGNode* sNode, std::set<const ICFGNode*>& togglePoints, std::set<const SVFGEdge*> &visited)
{
    FIFOWorkList<const SVFGNode*> worklist;
    worklist.push(sNode);
    
    while(!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
            it != eit; ++it)
        {
            SVFGEdge* edge = *it;
            SVFGNode* nextNode = edge->getDstNode();
            if(visited.find(edge) == visited.end()) {
                s32_t nKind = nextNode->getNodeKind();
                if(nKind == VFGNode::Store || nKind == VFGNode::Load)
                {
                    const ICFGNode* iNode = nextNode->getICFGNode();
                    if(togglePoints.find(iNode) == togglePoints.end()) {
                        togglePoints.insert(iNode);
                    }


                    if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(iNode))
                    {
                        Instruction* inst = const_cast<Instruction*>(intra->getInst());
                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "StackafterReturnAccess");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("StackafterReturnAccess", node);
                    }
                    else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode))
                    {
                        Instruction* inst = const_cast<Instruction*>(callNode->getCallSite());
                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "StackafterReturnAccess");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("StackafterReturnAccess", node);
                    }
                    else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(iNode))
                    {
                        Instruction* inst = const_cast<Instruction*>(RetNode->getCallSite());
                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "StackafterReturnAccess");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("StackafterReturnAccess", node);
                    }


                    
                }
                worklist.push(nextNode);
                visited.insert(edge);
            }


        }


    }

}


void ParallelS2FuzzStackAccessSites(const SVFGNode* sNode, std::set<const ICFGNode*>& togglePoints, std::set<const SVFGEdge*>& visited, std::mutex &mtx)
{
    FIFOWorkList<const SVFGNode*> worklist;
    worklist.push(sNode);
    
    const SVFFunction* stackFun = sNode->getFun();
    while(!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
            it != eit; ++it)
            {
                SVFGEdge* edge = *it;
                SVFGNode* nextNode = edge->getDstNode();
                if(visited.find(edge) == visited.end())
                {
                    if(const SVF::FormalRetSVFGNode* retNode = dyn_cast<SVF::FormalRetSVFGNode>(nextNode))
                    {
                        const SVFFunction* retFun = retNode->getFun();
                        if(retFun == stackFun)
                        {
                            std::lock_guard<std::mutex> lock(mtx);
                            // From here we track stack access site after return scope
                            S2FuzzTrackAccessSitesAfterReturn(nextNode, togglePoints, visited);
                        }
                    }
                    worklist.push(nextNode);
                    visited.insert(edge);
                }
            }
    }
}

void S2FuzzStackAccessSites(const SVFGNode* sNode, std::set<const ICFGNode*>& togglePoints, std::set<const SVFGEdge*>& visited)
{
    FIFOWorkList<const SVFGNode*> worklist;
    worklist.push(sNode);
    
    const SVFFunction* stackFun = sNode->getFun();
    while(!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
            it != eit; ++it)
            {
                SVFGEdge* edge = *it;
                SVFGNode* nextNode = edge->getDstNode();
                if(visited.find(edge) == visited.end())
                {
                    const SVFFunction* svfFun = nextNode->getFun();
                    if(svfFun) {
                        Function* unsafeCandidate = svfFun->getLLVMFun();
                        if(isUnsafelisted(unsafeCandidate)) {
                            // Mark all vulnerable memory access sites in the unsafe function
                            S2FuzzTrackUnsafeStackAccessSites(nextNode, togglePoints, visited);
                        }
                    }
                    if(const SVF::FormalRetSVFGNode* retNode = dyn_cast<SVF::FormalRetSVFGNode>(nextNode))
                    {
                        const SVFFunction* retFun = retNode->getFun();
                        if(retFun == stackFun)
                        {
                            // From here we track stack access site after return scope
                            S2FuzzTrackAccessSitesAfterReturn(nextNode, togglePoints, visited);
                        }
                    }
                    worklist.push(nextNode);
                    visited.insert(edge);
                }
            }
    }
}

bool isFunctionReachable(SVFModule* svfModule, PTACallGraph* callGraph, const Function* srcFunc, const Function* targetFunc) {
    std::set<const Function*> visited;
    std::queue<const Function*> worklist;

    worklist.push(srcFunc);
    visited.insert(srcFunc);

    while (!worklist.empty()) {
        const Function* currFunc = worklist.front();
        worklist.pop();

        if (currFunc == targetFunc) {
            return true;
        }

        PTACallGraphNode* currNode = callGraph->getCallGraphNode(svfModule->getSVFFunction(currFunc));
        if (!currNode) continue;

        for (PTACallGraphEdge::CallGraphEdgeSet::iterator it = currNode->OutEdgeBegin();
             it != currNode->OutEdgeEnd(); ++it)
        {
            PTACallGraphEdge* edge = (*it);
            PTACallGraphNode* dstNode = edge->getDstNode();
            const Function* dstFunc = dstNode->getFunction()->getLLVMFun();

            if (visited.find(dstFunc) == visited.end()) {
                visited.insert(dstFunc);
                worklist.push(dstFunc);
            }
        }
    }

    return false;
}

void S2FuzzTrackAccessSitesAfterDropOrig(const SVFGNode* sNode, std::set<const ICFGNode*>& togglePoints, std::set<const SVFGEdge*> &visited, std::set<const llvm::Function *> reachableFunctions, std::set<std::string> outermostFunctions, std::set<std::string>& outermostDropFunctions, std::set<std::string>* doubleDropfuncs)
{
    // for(const auto& func : outermostDropFunctions) {
    //     std::cout << "Outer Drop Function : " << func << std::endl;
    // }
    FIFOWorkList<const SVFGNode*> worklist;
    worklist.push(sNode);
    
    while(!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
            it != eit; ++it)
        {
            SVFGEdge* edge = *it;
            SVFGNode* nextNode = edge->getDstNode();

            if(visited.find(edge) == visited.end()) {
                s32_t nKind = nextNode->getNodeKind();

                worklist.push(nextNode);
                visited.insert(edge);

                if(nKind == VFGNode::Store || nKind == VFGNode::Load)
                {
                    const ICFGNode* iNode = nextNode->getICFGNode();

                    Instruction* inst = nullptr;
                    if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(iNode)) {
                        inst = const_cast<Instruction*>(intra->getInst());
                    }
                    else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode)) {
                        inst = const_cast<Instruction*>(callNode->getCallSite());
                    }
                    else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(iNode)) {
                        inst = const_cast<Instruction*>(RetNode->getCallSite());
                    }

                    if(inst) {
                        bool isDebugInstr = false;

                        for (auto& op : inst->operands()) {
                            if (Value* v = op.get()) {
                                if (v->hasName()) {
                                    StringRef name = v->getName();
                                    if (name.contains(".dbg.spill")) {
                                        isDebugInstr = true;
                                        break;
                                    }
                                }
                            }
                        }

                        if(isDebugInstr) continue;
                        if(reachableFunctions.find(inst->getFunction()) == reachableFunctions.end()) continue;
                        // bool reachable = false;
                        // for (const Function* outerFunc : outermostFunctions) {
                        //     if (isFunctionReachable(callgraph, outerFunc, inst->getFunction())) {
                        //         reachable = true;
                        //         break;
                        //     }
                        // }

                        // if(!reachable) continue;

                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "HeapafterDropAccess");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("HeapafterDropAccess", node);

                        if (togglePoints.find(iNode) == togglePoints.end()) {
                            togglePoints.insert(iNode);
                        }


                    }
                }

                else
                {
                    const ICFGNode* iNode = nextNode->getICFGNode();

                    if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode)) {

                        const Instruction* inst = callNode->getCallSite();
                        if(const CallInst* callInst = llvm::dyn_cast<CallInst>(inst)) {
                            const Function* calledFunc = callInst->getCalledFunction();
                            // std::cout << "Called Function : " << calledFunc->getName().str() << "\n";
                            if(calledFunc && calledFunc->getName().contains("drop_in_place") && outermostFunctions.find(calledFunc->getName().str()) != outermostFunctions.end()) {
                                // std::cout << "Called Function : " << calledFunc->getName().str() << "\n";
                                // if(outermostDropFunctions.find(calledFunc->getName().str()) == outermostDropFunctions.end()) {
                                //     // std::cout << "Drop Function : " << calledFunc->getName().str() << std::endl;
                                //     outermostDropFunctions.insert(calledFunc->getName().str());
                                // }
                                // else {
                                //     // std::cout << "Double Drop Function : " << calledFunc->getName().str() << std::endl;
                                //     // LLVMContext &context = inst->getContext();
                                //     // MDString *str = MDString::get(context, "DoubleDropSuspicious");
                                //     // MDNode *node = MDNode::get(context, str);
                                //     // const_cast<Instruction*>(inst)->setMetadata("DoubleDropSuspicious", node);
                                //     // std::cout << "Double Drop Function : " << calledFunc->getName().str() << std::endl;
                                //     doubleDropfuncs->insert(calledFunc->getName().str());

                                // }
                                doubleDropfuncs->insert(calledFunc->getName().str());
                            }
                        }



                        else if (const InvokeInst* invokeInst = llvm::dyn_cast<InvokeInst>(inst)) {
                            const Function* invokedFunc = invokeInst->getCalledFunction();
                            
                            if(invokedFunc && invokedFunc->getName().contains("drop_in_place") && outermostFunctions.find(invokedFunc->getName().str()) != outermostFunctions.end()) {
                                // std::cout << "Invoked Function : " << invokedFunc->getName().str() << "\n";
                                // if(outermostDropFunctions.find(invokedFunc->getName().str()) == outermostDropFunctions.end()) {
                                //     outermostDropFunctions.insert(invokedFunc->getName().str());
                                // }
                                // else {
                                //     doubleDropfuncs->insert(invokedFunc->getName().str());
                                // }
                                doubleDropfuncs->insert(invokedFunc->getName().str());
                            }
                        }
                    }
                }


            }


        }


    }

}


void S2FuzzTrackAccessSitesAfterDrop(const SVFGNode* sNode, std::set<const ICFGNode*>& togglePoints, std::set<const SVFGEdge*> &visited)
{

    FIFOWorkList<const SVFGNode*> worklist;
    worklist.push(sNode);

                
    // const ICFGNode* startNode = sNode->getICFGNode();
    // if(const SVF::CallBlockNode* cd = dyn_cast<SVF::CallBlockNode>(startNode)) {
    //     string startddd = startNode->toString();
    //     string first_startddd = extractDropInPlaceFunctionName(startddd);
    //     if(!first_startddd.empty()) {
    //         if(togglePoints.find(startNode) == togglePoints.end()) {
    //             togglePoints.insert(startNode);
    //         }

    //         Instruction* it = const_cast<Instruction*>(cd->getCallSite());
    //         LLVMContext &ct = it->getContext();
    //         MDString *dou = MDString::get(ct, "DoubleDropSuspicious");
    //         MDNode *ne = MDNode::get(ct, dou);
    //         it->setMetadata("DoubleDropSuspicious", ne);

    //     }
        
    // }
                
    
    while(!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
            it != eit; ++it)
        {
            SVFGEdge* edge = *it;
            SVFGNode* nextNode = edge->getDstNode();
            if(visited.find(edge) == visited.end()) {
                s32_t nKind = nextNode->getNodeKind();
                if(nKind == VFGNode::Store || nKind == VFGNode::Load)
                {
                    const ICFGNode* iNode = nextNode->getICFGNode();

                    Instruction* inst = nullptr;
                    if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(iNode)) {
                        inst = const_cast<Instruction*>(intra->getInst());
                    }
                    else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode)) {
                        inst = const_cast<Instruction*>(callNode->getCallSite());
                    }
                    else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(iNode)) {
                        inst = const_cast<Instruction*>(RetNode->getCallSite());
                    }

                    if(inst) {
                        bool isDebugInstr = false;

                        for (auto& op : inst->operands()) {
                            if (Value* v = op.get()) {
                                if (v->hasName()) {
                                    StringRef name = v->getName();
                                    if (name.endswith(".dbg.spill")) {
                                        isDebugInstr = true;
                                        break;
                                    }
                                }
                            }
                        }

                        if(isDebugInstr) continue;

                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "HeapafterDropAccess");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("HeapafterDropAccess", node);

                        if (togglePoints.find(iNode) == togglePoints.end()) {
                            togglePoints.insert(iNode);
                        }

                    }






                    // if(togglePoints.find(iNode) == togglePoints.end()) {
                    //     togglePoints.insert(iNode);
                    // }
                    
                    // if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(iNode))
                    // {
                    //     Instruction* inst = const_cast<Instruction*>(intra->getInst());
                    //     LLVMContext &context = inst->getContext();
                    //     MDString *str = MDString::get(context, "HeapafterDropAccess");
                    //     MDNode *node = MDNode::get(context, str);
                    //     inst->setMetadata("HeapafterDropAccess", node);
                    // }
                    // else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode))
                    // {
                    //     Instruction* inst = const_cast<Instruction*>(callNode->getCallSite());
                    //     LLVMContext &context = inst->getContext();
                    //     MDString *str = MDString::get(context, "HeapafterDropAccess");
                    //     MDNode *node = MDNode::get(context, str);
                    //     inst->setMetadata("HeapafterDropAccess", node);
                    // }
                    // else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(iNode))
                    // {
                    //     Instruction* inst = const_cast<Instruction*>(RetNode->getCallSite());
                    //     LLVMContext &context = inst->getContext();
                    //     MDString *str = MDString::get(context, "HeapafterDropAccess");
                    //     MDNode *node = MDNode::get(context, str);
                    //     inst->setMetadata("HeapafterDropAccess", node);
                    // }

                }

                


                // else if (nKind == VFGNode::AParm || nKind == VFGNode::FParm || nKind == VFGNode::APIN ) {
                //     const ICFGNode* iNode = nextNode->getICFGNode();
                //     if(const SVF::CallBlockNode* cNode = dyn_cast<SVF::CallBlockNode>(iNode)) {
                //         string dropstr = iNode->toString();
                //         string first_dropstr_start = extractDropInPlaceFunctionName(dropstr);
                //         if(!first_dropstr_start.empty()) {
                //             Instruction* inst = const_cast<Instruction*>(cNode->getCallSite());
                //             if(inst) {
                //                 bool isDebugInstr = false;
                //                 for (auto & op : inst->operands()) {
                //                     if(isa<DbgInfoIntrinsic>(op)) {
                //                         /* Debug Instruction */
                //                         std::cout << "Debug Instruction : ";
                //                         inst->print(errs());
                //                         isDebugInstr = true;
                //                         break;
                //                     }
                //                 }

                //                 if(isDebugInstr) continue;

                //                 LLVMContext &context = inst->getContext();
                //                 MDString *str = MDString::get(context, "DoubleDropSuspicious");
                //                 MDNode *node = MDNode::get(context, str);
                //                 inst->setMetadata("DoubleDropSuspicious", node);

                //                 if(togglePoints.find(iNode) == togglePoints.end()) {
                //                     togglePoints.insert(iNode);
                //                 }
                //             }


                //             // if(togglePoints.find(iNode) == togglePoints.end()) {
                //             //     togglePoints.insert(iNode);
                //             // }

                //             // Instruction* inst = const_cast<Instruction*>(cNode->getCallSite());
                //             // LLVMContext &context = inst->getContext();
                //             // MDString *str = MDString::get(context, "DoubleDropSuspicious");
                //             // MDNode *node = MDNode::get(context, str);
                //             // inst->setMetadata("DoubleDropSuspicious", node);

                //         }
                        
                //     }
                // }


                const ICFGNode* startNode = nextNode->getICFGNode();
                if(const SVF::CallBlockNode* cd = dyn_cast<SVF::CallBlockNode>(startNode)) {
                    string startddd = startNode->toString();
                    string first_startddd = extractDropInPlaceFunctionName(startddd);
                    if(!first_startddd.empty()) {
                        if(togglePoints.find(startNode) == togglePoints.end()) {
                            togglePoints.insert(startNode);
                        }

                        Instruction* it = const_cast<Instruction*>(cd->getCallSite());
                        LLVMContext &ct = it->getContext();
                        MDString *dou = MDString::get(ct, "DoubleDropSuspicious");
                        MDNode *ne = MDNode::get(ct, dou);
                        it->setMetadata("DoubleDropSuspicious", ne);

                    }
                    
                }


                worklist.push(nextNode);
                visited.insert(edge);
            }


        }


    }

}

std::string extractDropInPlaceFunctionName(std::string& str) {
    std::regex re(R"(@\"(\S*drop_in_place\S*)\"\(\S*)");
    std::smatch match;
    if (std::regex_search(str, match, re)) {
        return match.str(1);
    }
    return "";
}

void ParallelS2FuzzHeapAccessSites(const SVFGNode* sNode, std::set<const ICFGNode*>& togglePoints, std::set<const SVFGEdge*>& visited, std::mutex &mtx, std::set<const llvm::Function *> reachableFunctions)
{
    // ofstream myfile;
    // myfile.open("AfterHeapAccessDebug.txt", ios::out | ios::app);
    FIFOWorkList<const SVFGNode*> worklist;
    // std::set<const SVFGEdge*> visited;
    worklist.push(sNode);

    while(!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        
        const ICFGNode* iNode = vNode->getICFGNode();

        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
            it != eit; ++it)
            {
                SVFGEdge* edge = *it;
                SVFGNode* nextNode = edge->getDstNode();
                if(visited.find(edge) == visited.end())
                {
                    // myfile << nextNode->toString() << "\n";
                    s32_t nKind = nextNode->getNodeKind();
                    
                    if(nKind == VFGNode::AParm || nKind == VFGNode::FParm) {
                        const ICFGNode* iNode = nextNode->getICFGNode();
                        
                        if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode)) {
                            string dropstr = iNode->toString();
                            string first_dropstr_start = extractDropInPlaceFunctionName(dropstr);
                            if(!first_dropstr_start.empty()) {
                                std::lock_guard<std::mutex> lock(mtx);
                                S2FuzzTrackAccessSitesAfterDrop(nextNode, togglePoints, visited);
                                
                            }
                            

                        }
                    }


                    {
                        std::lock_guard<std::mutex> lock(mtx);
                        string dropstr_orig = nextNode->toString();
                        if(dropstr_orig.find("drop_in_place") != std::string::npos) {
                            // S2FuzzTrackAccessSitesAfterDropOrig(nextNode, togglePoints, visited, reachableFunctions);
                        }
                    }

                    
                    worklist.push(nextNode);
                    visited.insert(edge);
                }
            }

    }
    // myfile.close();

}



// void AblationModule1_TrackAccessSitesAfterDrop(const SVFGNode* sNode, std::set<const ICFGNode*>& togglePoints, std::set<const SVFGEdge*> &visited)
// {

//     FIFOWorkList<const SVFGNode*> worklist;
//     worklist.push(sNode);

                
//     const ICFGNode* startNode = sNode->getICFGNode();
//     if(const SVF::CallBlockNode* cd = dyn_cast<SVF::CallBlockNode>(startNode)) {
//         string startddd = startNode->toString();
//         string first_startddd = extractDropInPlaceFunctionName(startddd);
//         if(!first_startddd.empty()) {
//             if(togglePoints.find(startNode) == togglePoints.end()) {
//                 togglePoints.insert(startNode);
//             }

//             Instruction* it = const_cast<Instruction*>(cd->getCallSite());
//             LLVMContext &ct = it->getContext();
//             MDString *dou = MDString::get(ct, "DoubleDropSuspicious");
//             MDNode *ne = MDNode::get(ct, dou);
//             it->setMetadata("DoubleDropSuspicious", ne);

//         }
        
//     }
                
    
//     while(!worklist.empty())
//     {
//         const SVFGNode* vNode = worklist.pop();
//         for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
//             it != eit; ++it)
//         {
//             SVFGEdge* edge = *it;
//             SVFGNode* nextNode = edge->getDstNode();
//             if(visited.find(edge) == visited.end()) {
//                 s32_t nKind = nextNode->getNodeKind();
//                 if(nKind == VFGNode::Store || nKind == VFGNode::Load)
//                 {
//                     const ICFGNode* iNode = nextNode->getICFGNode();
//                     if(togglePoints.find(iNode) == togglePoints.end()) {
//                         togglePoints.insert(iNode);
//                     }
                    
//                     if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(iNode))
//                     {
//                         Instruction* inst = const_cast<Instruction*>(intra->getInst());
//                         LLVMContext &context = inst->getContext();
//                         MDString *str = MDString::get(context, "HeapafterDropAccess");
//                         MDNode *node = MDNode::get(context, str);
//                         inst->setMetadata("HeapafterDropAccess", node);
//                     }
//                     else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode))
//                     {
//                         Instruction* inst = const_cast<Instruction*>(callNode->getCallSite());
//                         LLVMContext &context = inst->getContext();
//                         MDString *str = MDString::get(context, "HeapafterDropAccess");
//                         MDNode *node = MDNode::get(context, str);
//                         inst->setMetadata("HeapafterDropAccess", node);
//                     }
//                     else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(iNode))
//                     {
//                         Instruction* inst = const_cast<Instruction*>(RetNode->getCallSite());
//                         LLVMContext &context = inst->getContext();
//                         MDString *str = MDString::get(context, "HeapafterDropAccess");
//                         MDNode *node = MDNode::get(context, str);
//                         inst->setMetadata("HeapafterDropAccess", node);
//                     }

//                 }

//                 else if (nKind == VFGNode::AParm || nKind == VFGNode::FParm || nKind == VFGNode::APIN ) {
//                     const ICFGNode* iNode = nextNode->getICFGNode();
//                     if(const SVF::CallBlockNode* cNode = dyn_cast<SVF::CallBlockNode>(iNode)) {
//                         string dropstr = iNode->toString();
//                         string first_dropstr_start = extractDropInPlaceFunctionName(dropstr);
//                         if(!first_dropstr_start.empty()) {
//                             if(togglePoints.find(iNode) == togglePoints.end()) {
//                                 togglePoints.insert(iNode);
//                             }

//                             Instruction* inst = const_cast<Instruction*>(cNode->getCallSite());
//                             LLVMContext &context = inst->getContext();
//                             MDString *str = MDString::get(context, "DoubleDropSuspicious");
//                             MDNode *node = MDNode::get(context, str);
//                             inst->setMetadata("DoubleDropSuspicious", node);

//                         }
                        
//                     }
//                 }


//                 worklist.push(nextNode);
//                 visited.insert(edge);
//             }


//         }


//     }

// }

// void AblationModule1_TrackAccessSitesAfterDropOrig(const SVFGNode* sNode, std::set<const ICFGNode*>& togglePoints, std::set<const SVFGEdge*> &visited)
// {
//     FIFOWorkList<const SVFGNode*> worklist;
//     worklist.push(sNode);
    
//     while(!worklist.empty())
//     {
//         const SVFGNode* vNode = worklist.pop();
//         for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
//             it != eit; ++it)
//         {
//             SVFGEdge* edge = *it;
//             SVFGNode* nextNode = edge->getDstNode();
//             if(visited.find(edge) == visited.end()) {
//                 s32_t nKind = nextNode->getNodeKind();
//                 if(nKind == VFGNode::Store || nKind == VFGNode::Load)
//                 {
//                     const ICFGNode* iNode = nextNode->getICFGNode();
//                     if(togglePoints.find(iNode) == togglePoints.end()) {
//                         togglePoints.insert(iNode);
//                     }
                    
//                     if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(iNode))
//                     {
//                         Instruction* inst = const_cast<Instruction*>(intra->getInst());
//                         LLVMContext &context = inst->getContext();
//                         MDString *str = MDString::get(context, "HeapafterDropAccess");
//                         MDNode *node = MDNode::get(context, str);
//                         inst->setMetadata("HeapafterDropAccess", node);
//                     }
//                     else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode))
//                     {
//                         Instruction* inst = const_cast<Instruction*>(callNode->getCallSite());
//                         LLVMContext &context = inst->getContext();
//                         MDString *str = MDString::get(context, "HeapafterDropAccess");
//                         MDNode *node = MDNode::get(context, str);
//                         inst->setMetadata("HeapafterDropAccess", node);
//                     }
//                     else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(iNode))
//                     {
//                         Instruction* inst = const_cast<Instruction*>(RetNode->getCallSite());
//                         LLVMContext &context = inst->getContext();
//                         MDString *str = MDString::get(context, "HeapafterDropAccess");
//                         MDNode *node = MDNode::get(context, str);
//                         inst->setMetadata("HeapafterDropAccess", node);
//                     }

//                 }

//                 worklist.push(nextNode);
//                 visited.insert(edge);
//             }


//         }


//     }

// }


// void AblationModule1_HeapAccessSites(const SVFGNode* sNode, std::set<const ICFGNode*>& togglePoints, std::set<const SVFGEdge*>& visited)
// {
//     FIFOWorkList<const SVFGNode*> worklist;
//     worklist.push(sNode);

//     while(!worklist.empty()) 
//     {
//         const SVFGNode* vNode = worklist.pop();
//         const ICFGNode* iNode = vNode->getICFGNode();

//         for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd(); it != eit; ++it)
//         {
//             SVFGEdge* edge = *it;
//             SVFGNode* nextNode = edge->getDstNode();
//             if(visited.find(edge) == visited.end()) {
//                 s32_t nKind = nextNode->getNodeKind();
//                 if(nKind == VFGNode::AParm || nKind == VFGNode::FParm) {
//                     const ICFGNode* iNode = nextNode->getICFGNode();
//                     if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode)) {
//                         string dropstr = iNode->toString();
//                         string first_dropstr_start = extractDropInPlaceFunctionName(dropstr);
//                         if(!first_dropstr_start.empty()) {
//                             AblationModule1_TrackAccessSitesAfterDrop(nextNode, togglePoints, visited);
//                         }
//                     }
//                 }

//                 string dropstr_orig = nextNode->toString();
//                 if(dropstr_orig.find("drop_in_place") != std::string::npos) {
//                     AblationModule1_TrackAccessSitesAfterDropOrig(nextNode, togglePoints, visited);
//                 }

//                 worklist.push(nextNode);
//                 visited.insert(edge);
//             }



//         }
//     }


// }



void ParallelS2FuzzTrackAccessSitesAfterDropOrig(
    const SVFGNode* sNode,
    std::set<const ICFGNode*>& togglePoints,
    std::set<const SVFGEdge*>& visited,
    const std::set<const llvm::Function*>& reachableFunctions,
    const std::set<std::string>& outerFunctions,
    std::set<std::string>& localOutermostDropFunctions,   
    std::set<std::string>& localDoubleDropFunctions,     
    std::mutex &visitedMutex,
    std::mutex &togglePointsMutex
) {
    FIFOWorkList<const SVFGNode*> worklist;
    worklist.push(sNode);

    while(!worklist.empty()) {
        const SVFGNode* vNode = worklist.pop();
        for(auto it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd(); it != eit; ++it) {
            SVFGEdge* edge = *it;
            SVFGNode* nextNode = edge->getDstNode();

            {
                std::lock_guard<std::mutex> lock(visitedMutex);
                if(visited.find(edge) != visited.end()) continue;
                visited.insert(edge);
            }

            s32_t nKind = nextNode->getNodeKind();
            worklist.push(nextNode);

            if(nKind == VFGNode::Store || nKind == VFGNode::Load) {
                const ICFGNode* iNode = nextNode->getICFGNode();
                Instruction* inst = nullptr;
                if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(iNode)) {
                    inst = const_cast<Instruction*>(intra->getInst());
                }
                else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode)) {
                    inst = const_cast<Instruction*>(callNode->getCallSite());
                }
                else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(iNode)) {
                    inst = const_cast<Instruction*>(RetNode->getCallSite());
                }

                if(inst) {
                    bool isDebugInstr = false;
                    for (auto& op : inst->operands()) {
                        if (Value* v = op.get()) {
                            if (v->hasName() && v->getName().contains(".dbg.spill")) {
                                isDebugInstr = true;
                                break;
                            }
                        }
                    }

                    if(isDebugInstr) continue;
                    if(reachableFunctions.find(inst->getFunction()) == reachableFunctions.end()) continue;

                    LLVMContext &context = inst->getContext();
                    MDString *str = MDString::get(context, "HeapafterDropAccess");
                    MDNode *node = MDNode::get(context, str);

                    {
                        std::lock_guard<std::mutex> lock(togglePointsMutex);
                        inst->setMetadata("HeapafterDropAccess", node);
                        togglePoints.insert(iNode);
                    }
                }
            } else {
                const ICFGNode* iNode = nextNode->getICFGNode();
                if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode)) {
                    const Instruction* inst = callNode->getCallSite();
                    if(const CallInst* callInst = llvm::dyn_cast<CallInst>(inst)) {
                        const Function* calledFunc = callInst->getCalledFunction();
                        if(calledFunc && calledFunc->getName().contains("drop_in_place") && 
                           outerFunctions.find(calledFunc->getName().str()) != outerFunctions.end()) 
                        {
                            localDoubleDropFunctions.insert(calledFunc->getName().str());
                            // localOutermostDropFunctions.insert(calledFunc->getName().str());
                        }
                    } else if (const InvokeInst* invokeInst = llvm::dyn_cast<InvokeInst>(inst)) {
                        const Function* invokedFunc = invokeInst->getCalledFunction();
                        if(invokedFunc && invokedFunc->getName().contains("drop_in_place") && 
                           outerFunctions.find(invokedFunc->getName().str()) != outerFunctions.end()) 
                        {
                            localDoubleDropFunctions.insert(invokedFunc->getName().str());
                            // localOutermostDropFunctions.insert(invokedFunc->getName().str());
                        }
                    }
                }
            }
        }
    }
}

void ParallelS2FuzzHeapAccessSitesWrapper(
    const SVFGNode* sNode,
    std::set<const ICFGNode*>& togglePoints,
    std::mutex &togglePointsMutex,
    const std::set<std::string>& outerFunctions,
    std::set<const SVFGNode*>& NodesForAfterDrop,
    std::mutex &NodesForAfterDropMutex

) {
    FIFOWorkList<const SVFGNode*> worklist;
    worklist.push(sNode);
    std::set<const SVFGEdge*> visited;

    while(!worklist.empty()) {
        const SVFGNode* vNode = worklist.pop();

        for(auto it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd(); it != eit; ++it) {
            SVFGEdge* edge = *it;
            SVFGNode* nextNode = edge->getDstNode();
            if(visited.find(edge) == visited.end()) {
                const ICFGNode* nextICFGNode = nextNode->getICFGNode();
                if(const SVF::CallBlockNode* callNode = llvm::dyn_cast<SVF::CallBlockNode>(nextICFGNode)) {
                    const Instruction* inst = callNode->getCallSite();
                    const Function* calledFunc = nullptr;
                    if(const CallInst* callInst = llvm::dyn_cast<CallInst>(inst)) {
                        calledFunc = callInst->getCalledFunction();
                    } else if (const InvokeInst* invokeInst = llvm::dyn_cast<InvokeInst>(inst)) {
                        calledFunc = invokeInst->getCalledFunction();
                    }

                    if(calledFunc && calledFunc->getName().contains("drop_in_place") 
                    && outerFunctions.find(calledFunc->getName().str()) != outerFunctions.end()) 
                    {
                        const BasicBlock* nextBB = nextICFGNode->getBB();
                        if(panicBBs.find(nextBB) != panicBBs.end()) {
                            std::lock_guard<std::mutex> lock(togglePointsMutex);
                            togglePoints.insert(nextICFGNode);
                        }

                        {
                            std::lock_guard<std::mutex> lock(NodesForAfterDropMutex);
                            NodesForAfterDrop.insert(nextNode);
                        }

                        // ParallelS2FuzzTrackAccessSitesAfterDropOrig(
                        //     nextNode, togglePoints, visited,
                        //     reachableFunctions, outerFunctions,
                        //     localOutermostDropFunctions, localDoubleDropFunctions,
                        //     visitedMutex, togglePointsMutex
                        // );
                    }
                }
                worklist.push(nextNode);
                visited.insert(edge);
            }

            
        }
    }

    // {
    //     std::lock_guard<std::mutex> lockDouble(doubleDropFunctionsMutex);
    //     doubleDropFunctions->insert(localDoubleDropFunctions.begin(), localDoubleDropFunctions.end());
    // }

    // {
    //     std::lock_guard<std::mutex> lockOutermost(outermostDropFunctionsMutex);
    //     outermostDropFunctions.insert(localOutermostDropFunctions.begin(), localOutermostDropFunctions.end());
    // }
}



// void S2FuzzHeapAccessSites(const SVFGNode* sNode, std::set<const ICFGNode*>& togglePoints, std::set<const SVFGEdge*>& visited)
// {
//     // ofstream myfile;
//     // myfile.open("HeapAccessDebug.txt", ios::out | ios::app);

//     FIFOWorkList<const SVFGNode*> worklist;
//     // std::set<const SVFGEdge*> visited;
//     worklist.push(sNode);

//     /* Debugging */
//     // std::string sNodeName = sNode->toString();
//     // // myfile << "-----------------------------------------\n";
//     // // myfile << "Start Node : " << sNodeName << "\n";

//     // FIFOWorkList<const SVFGNode*> worklist_debug;
//     // worklist_debug.push(sNode);
//     // std::set<const SVFGNode*> visited_debug;

//     // while(!worklist_debug.empty()) {
//     //     const SVFGNode* vNode_debug = worklist_debug.pop();
//     //     // myfile << "Debug Node : " << vNode_debug->toString() << "\n";
        
//     //     for(SVFGNode::const_iterator it = vNode_debug->OutEdgeBegin(), eit = vNode_debug->OutEdgeEnd();
//     //         it != eit; ++it) {
//     //         SVFGEdge* edge = *it;
//     //         SVFGNode* nextNode = edge->getDstNode();
//     //         // myfile << "Debug Node : " << nextNode->toString() << "\n";
//     //         worklist_debug.push(nextNode);
//     //         visited_debug.insert(nextNode);
//     //     }
//     // }

//     while(!worklist.empty())
//     {
//         const SVFGNode* vNode = worklist.pop();
        
//         const ICFGNode* iNode = vNode->getICFGNode();

//         for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
//             it != eit; ++it)
//             {
//                 SVFGEdge* edge = *it;
//                 SVFGNode* nextNode = edge->getDstNode();
//                 if(visited.find(edge) == visited.end())
//                 {
//                     // myfile << nextNode->toString() << "\n";
//                     s32_t nKind = nextNode->getNodeKind();
                    
//                     // if(nKind == VFGNode::AParm || nKind == VFGNode::FParm) {
//                     //     const ICFGNode* iNode = nextNode->getICFGNode();
                        
//                     //     if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode)) {
//                     //         string dropstr = iNode->toString();
//                     //         string first_dropstr_start = extractDropInPlaceFunctionName(dropstr);
//                     //         if(!first_dropstr_start.empty()) {
//                     //             S2FuzzTrackAccessSitesAfterDrop(nextNode, togglePoints, visited);
                                
//                     //         }
                            

//                     //     }
//                     // }

//                     string dropstr_orig = nextNode->toString();
//                     if(dropstr_orig.find("drop_in_place") != std::string::npos) {
//                         S2FuzzTrackAccessSitesAfterDropOrig(nextNode, togglePoints, visited);
//                     }

                    
//                     worklist.push(nextNode);
//                     visited.insert(edge);
//                 }
//             }

//     }
//     // myfile.close();

// }



/* 
    (1) sNode로부터 노드 탐색 시작
    (2) 노드가 call instruction일 경우, callinst인지 invokeinst인지 판단
    (3) call instruction일 경우, 해당 함수가 drop_in_place인지 판단
    (4) drop_in_place일 경우, 해당 함수가 outerFunctions에 존재하는지 판단
    (5) outerFunctions에 존재하는 경우, 해당 함수가 panicBBs에 존재하는지 판단
    (6) panicBBs에 존재하는 경우, 해당 노드를 togglePoints에 삽입
    (7) outerFunctions에 존재하는 경우, 해당 노드를 outermostDropFunctions에 삽입
    (8) 이후 S2FuzzTrackAccessSitesAfterDropOrig 호출
*/ 
void S2FuzzHeapAccessSites(const SVFGNode* sNode, std::set<const ICFGNode*>& togglePoints, std::set<const SVFGEdge*>& visited, std::set<const llvm::Function *> reachableFunctions, std::set<std::string> outerFunctions, std::set<std::string>* doubleDropFunctions)
{
    // if(sNode->getFun()->getLLVMFun()->getName().str().find("ZN17RUSTSEC_2020_00604main17hf0bc7c49b35") != std::string::npos && sNode->toString().find("string1") != std::string::npos) {
    //     return;
    // }
    // if(sNode->getId() != 85) {
    //     return;
    // }

    

    FIFOWorkList<const SVFGNode*> worklist;
    worklist.push(sNode);
    // std::cout << "Start Node : " << sNode->toString() << "\n";

    std::set<std::string> outermostDropFunctions;
    // std::set<const SVFGEdge*> visited;

    while(!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        // const ICFGNode* iNode = vNode->getICFGNode(); // Not used here

        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd(); it != eit; ++it)
        {
            SVFGEdge* edge = *it;
            SVFGNode* nextNode = edge->getDstNode();
            // std::cout << "Next Node : " << nextNode->toString() << "\n";
            if(visited.find(edge) == visited.end())
            {
                // Get the ICFGNode associated with nextNode
                const ICFGNode* nextICFGNode = nextNode->getICFGNode();

                // Check if nextICFGNode is a CallBlockNode
                if(const SVF::CallBlockNode* callNode = llvm::dyn_cast<SVF::CallBlockNode>(nextICFGNode))
                {
                    // Get the call instruction
                    const Instruction* inst = callNode->getCallSite();
                    // std::cout << "Call Function : " << inst->getFunction()->getName().str() << std::endl;
                    const Function* calledFunc = nullptr;
                    if(const CallInst* callInst = llvm::dyn_cast<CallInst>(inst))
                    {
                        // Get the called function
                        calledFunc = callInst->getCalledFunction();

                    }
                    else if (const InvokeInst* invokeInst = llvm::dyn_cast<InvokeInst>(inst)) {
                        calledFunc = invokeInst->getCalledFunction();
                    }

                    if(calledFunc && isUnsafelisted(calledFunc)) {
                        // Mark all vulnerable memory access sites in unsafe function
                        S2FuzzTrackUnsafeHeapAccessSites(nextNode, togglePoints, visited, reachableFunctions);
                    }
                    

                    if(calledFunc && calledFunc->getName().contains("drop_in_place") && outerFunctions.find(calledFunc->getName().str()) != outerFunctions.end())
                    {
    
                        const BasicBlock* nextBB = nextICFGNode->getBB();
                        if(panicBBs.find(nextBB) != panicBBs.end()) {
                            togglePoints.insert(nextICFGNode);
                        }

                        /* Debugging */
                        // std::cout << "Drop in Place Call : " << calledFunc->getName().str() << "\n";
                        // We have encountered a call to drop_in_place
                        // Call your function to process memory accesses after the drop

                        
                        

                        outermostDropFunctions.insert(calledFunc->getName().str());
                        S2FuzzTrackAccessSitesAfterDropOrig(nextNode, togglePoints, visited, reachableFunctions, outerFunctions, outermostDropFunctions, doubleDropFunctions);

                    }


                }


                // Continue traversal
                worklist.push(nextNode);
                visited.insert(edge);
            }
        }
    }

}


void S2FuzzTrackAccessSitesAfterReturnAsyncSafe(const SVFGNode* sNode, std::set<const ICFGNode*>& togglePoints, std::set<const SVFGEdge*> &visited)
{
    FIFOWorkList<const SVFGNode*> worklist;
    worklist.push(sNode);
    
    while(!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
            it != eit; ++it)
        {
            SVFGEdge* edge = *it;
            SVFGNode* nextNode = edge->getDstNode();
            if(visited.find(edge) == visited.end()) {
                s32_t nKind = nextNode->getNodeKind();
                if(nKind == VFGNode::Store || nKind == VFGNode::Load)
                {
                    const ICFGNode* iNode = nextNode->getICFGNode();
                    if(togglePoints.find(iNode) == togglePoints.end()) {
                        togglePoints.insert(iNode);
                    }

                    if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(iNode))
                    {
                        Instruction* inst = const_cast<Instruction*>(intra->getInst());
                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "StackAccessAsyncSafe");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("StackAccessAsyncSafe", node);
                    }
                    else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode))
                    {
                        Instruction* inst = const_cast<Instruction*>(callNode->getCallSite());
                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "StackAccessAsyncSafe");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("StackAccessAsyncSafe", node);
                    }
                    else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(iNode))
                    {
                        Instruction* inst = const_cast<Instruction*>(RetNode->getCallSite());
                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "StackAccessAsyncSafe");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("StackAccessAsyncSafe", node);
                    }

                }
                worklist.push(nextNode);
                visited.insert(edge);
            }


        }


    }

}

void S2FuzzTrackUnsafeStackAccessSites(const SVFGNode* sNode, std::set<const ICFGNode*>& togglePoints, std::set<const SVFGEdge*> &visited)
{
    FIFOWorkList<const SVFGNode*> worklist;
    worklist.push(sNode);
    
    while(!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
            it != eit; ++it)
        {
            SVFGEdge* edge = *it;
            SVFGNode* nextNode = edge->getDstNode();
            if(visited.find(edge) == visited.end()) {
                s32_t nKind = nextNode->getNodeKind();
                if(nKind == VFGNode::Store || nKind == VFGNode::Load)
                {
                    const ICFGNode* iNode = nextNode->getICFGNode();
                    if(togglePoints.find(iNode) == togglePoints.end()) {
                        togglePoints.insert(iNode);
                    }

                    if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(iNode))
                    {
                        Instruction* inst = const_cast<Instruction*>(intra->getInst());
                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "UnsafeStackAccessSites");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("UnsafeStackAccessSites", node);
                    }
                    else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode))
                    {
                        Instruction* inst = const_cast<Instruction*>(callNode->getCallSite());
                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "UnsafeStackAccessSites");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("UnsafeStackAccessSites", node);
                    }
                    else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(iNode))
                    {
                        Instruction* inst = const_cast<Instruction*>(RetNode->getCallSite());
                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "UnsafeStackAccessSites");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("UnsafeStackAccessSites", node);
                    }

                }
                worklist.push(nextNode);
                visited.insert(edge);
            }


        }


    }

}


void AsyncSafeS2FuzzStackAccessSites(const SVFGNode* sNode, std::set<const ICFGNode*>& togglePoints, std::set<const SVFGEdge*>& visited)
{
    FIFOWorkList<const SVFGNode*> worklist;
    worklist.push(sNode);
    
    const SVFFunction* stackFun = sNode->getFun();
    while(!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
            it != eit; ++it)
            {
                SVFGEdge* edge = *it;
                SVFGNode* nextNode = edge->getDstNode();
                if(visited.find(edge) == visited.end())
                {
                    const SVFFunction* svfFun = nextNode->getFun();
                    if(svfFun) {
                        Function* asyncCandidate = svfFun->getLLVMFun();
                        if(isAsynclisted(asyncCandidate)) {
                            // Mark all vulnerable memory access sites in the async function
                            S2FuzzTrackAccessSitesAfterReturnAsyncSafe(nextNode, togglePoints, visited);
                        }
                    }


                    if(const SVF::FormalRetSVFGNode* retNode = dyn_cast<SVF::FormalRetSVFGNode>(nextNode))
                    {
                        const SVFFunction* retFun = retNode->getFun();
                        if(retFun == stackFun)
                        {
                            // From here we track stack access site after return scope
                            S2FuzzTrackAccessSitesAfterReturn(nextNode, togglePoints, visited);
                        }
                    }
                    worklist.push(nextNode);
                    visited.insert(edge);
                }
            }
    }
}

void S2FuzzTrackAccessSitesAfterDropAsyncSafe(const SVFGNode* sNode, std::set<const ICFGNode*>& togglePoints, std::set<const SVFGEdge*> &visited, std::set<const llvm::Function *> reachableFunctions)
{

    FIFOWorkList<const SVFGNode*> worklist;
    worklist.push(sNode);
    
    while(!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
            it != eit; ++it)
        {
            SVFGEdge* edge = *it;
            SVFGNode* nextNode = edge->getDstNode();

            if(visited.find(edge) == visited.end()) {
                s32_t nKind = nextNode->getNodeKind();

                worklist.push(nextNode);
                visited.insert(edge);

                if(nKind == VFGNode::Store || nKind == VFGNode::Load)
                {
                    const ICFGNode* iNode = nextNode->getICFGNode();

                    Instruction* inst = nullptr;
                    if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(iNode)) {
                        inst = const_cast<Instruction*>(intra->getInst());
                    }
                    else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode)) {
                        inst = const_cast<Instruction*>(callNode->getCallSite());
                    }
                    else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(iNode)) {
                        inst = const_cast<Instruction*>(RetNode->getCallSite());
                    }

                    if(inst) {
                        bool isDebugInstr = false;
                        for (auto& op : inst->operands()) {
                            if (Value* v = op.get()) {
                                if (v->hasName()) {
                                    StringRef name = v->getName();
                                    if (name.contains(".dbg.spill")) {
                                        isDebugInstr = true;
                                        break;
                                    }
                                }
                            }
                        }

                        if(isDebugInstr) continue;
                        if(reachableFunctions.find(inst->getFunction()) == reachableFunctions.end()) continue;

                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "HeapAccessAsyncSafe");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("HeapAccessAsyncSafe", node);

                        if (togglePoints.find(iNode) == togglePoints.end()) {
                            togglePoints.insert(iNode);
                        }


                    }
                }

            }


        }


    }

}


void S2FuzzTrackUnsafeHeapAccessSites(const SVFGNode* sNode, std::set<const ICFGNode*>& togglePoints, std::set<const SVFGEdge*> &visited, std::set<const llvm::Function *> reachableFunctions)
{

    FIFOWorkList<const SVFGNode*> worklist;
    worklist.push(sNode);
    
    while(!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
            it != eit; ++it)
        {
            SVFGEdge* edge = *it;
            SVFGNode* nextNode = edge->getDstNode();

            if(visited.find(edge) == visited.end()) {
                s32_t nKind = nextNode->getNodeKind();

                worklist.push(nextNode);
                visited.insert(edge);

                if(nKind == VFGNode::Store || nKind == VFGNode::Load)
                {
                    const ICFGNode* iNode = nextNode->getICFGNode();

                    Instruction* inst = nullptr;
                    if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(iNode)) {
                        inst = const_cast<Instruction*>(intra->getInst());
                    }
                    else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode)) {
                        inst = const_cast<Instruction*>(callNode->getCallSite());
                    }
                    else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(iNode)) {
                        inst = const_cast<Instruction*>(RetNode->getCallSite());
                    }

                    if(inst) {
                        bool isDebugInstr = false;
                        for (auto& op : inst->operands()) {
                            if (Value* v = op.get()) {
                                if (v->hasName()) {
                                    StringRef name = v->getName();
                                    if (name.contains(".dbg.spill")) {
                                        isDebugInstr = true;
                                        break;
                                    }
                                }
                            }
                        }

                        if(isDebugInstr) continue;
                        if(reachableFunctions.find(inst->getFunction()) == reachableFunctions.end()) continue;

                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "UnsafeHeapAccessSites");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("UnsafeHeapAccessSites", node);

                        if (togglePoints.find(iNode) == togglePoints.end()) {
                            togglePoints.insert(iNode);
                        }


                    }
                }

            }


        }


    }

}


void AsyncSafeS2FuzzHeapAccessSites(const SVFGNode* sNode, std::set<const ICFGNode*>& togglePoints, std::set<const SVFGEdge*>& visited, std::set<const llvm::Function *> reachableFunctions, std::set<std::string> outerFunctions, std::set<std::string>* doubleDropFunctions)
{

    FIFOWorkList<const SVFGNode*> worklist;
    worklist.push(sNode);
    // std::cout << "Start Node : " << sNode->toString() << "\n";

    std::set<std::string> outermostDropFunctions;
    // std::set<const SVFGEdge*> visited;

    while(!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        // const ICFGNode* iNode = vNode->getICFGNode(); // Not used here

        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd(); it != eit; ++it)
        {
            SVFGEdge* edge = *it;
            SVFGNode* nextNode = edge->getDstNode();
            // std::cout << "Next Node : " << nextNode->toString() << "\n";
            if(visited.find(edge) == visited.end())
            {
                // Get the ICFGNode associated with nextNode
                const ICFGNode* nextICFGNode = nextNode->getICFGNode();

                // Check if nextICFGNode is a CallBlockNode
                if(const SVF::CallBlockNode* callNode = llvm::dyn_cast<SVF::CallBlockNode>(nextICFGNode))
                {
                    // Get the call instruction
                    const Instruction* inst = callNode->getCallSite();
                    // std::cout << "Call Function : " << inst->getFunction()->getName().str() << std::endl;
                    const Function* calledFunc = nullptr;
                    if(const CallInst* callInst = llvm::dyn_cast<CallInst>(inst))
                    {
                        // Get the called function
                        calledFunc = callInst->getCalledFunction();

                    }
                    else if (const InvokeInst* invokeInst = llvm::dyn_cast<InvokeInst>(inst)) {
                        calledFunc = invokeInst->getCalledFunction();
                    }

                    if(calledFunc && isAsynclisted(calledFunc)) {
                        // Mark all vulnerable memory access sites in the async function
                        S2FuzzTrackAccessSitesAfterDropAsyncSafe(nextNode, togglePoints, visited, reachableFunctions);
                    }
                    

                    if(calledFunc && calledFunc->getName().contains("drop_in_place") && outerFunctions.find(calledFunc->getName().str()) != outerFunctions.end())
                    {
    
                        const BasicBlock* nextBB = nextICFGNode->getBB();
                        if(panicBBs.find(nextBB) != panicBBs.end()) {
                            togglePoints.insert(nextICFGNode);
                        }

                        /* Debugging */
                        // std::cout << "Drop in Place Call : " << calledFunc->getName().str() << "\n";
                        // We have encountered a call to drop_in_place
                        // Call your function to process memory accesses after the drop

                        
                        

                        outermostDropFunctions.insert(calledFunc->getName().str());
                        S2FuzzTrackAccessSitesAfterDropOrig(nextNode, togglePoints, visited, reachableFunctions, outerFunctions, outermostDropFunctions, doubleDropFunctions);

                    }


                }


                // Continue traversal
                worklist.push(nextNode);
                visited.insert(edge);
            }
        }
    }

}




std::set<const Instruction*> processRawPtrSVFGNode(const SVFGNode* sNode, std::set<const SVFGEdge*>& visited) {
    // 
    // const SVF::Function* parentFunction = sNode->getFun()->getLLVMFun();
    // std::cout << "Parent Function : " << parentFunction->getName().str() << "\n";

    std::set<const Instruction*> rawptrArgs;
    std::queue<const SVFGNode*> worklist;
    worklist.push(sNode);

    while (!worklist.empty()) {
        const SVFGNode* node = worklist.front();
        worklist.pop();

        s32_t nKind = node->getNodeKind();

        if(nKind == VFGNode::AParm || nKind == VFGNode::FParm) {
            const ICFGNode* iNode = node->getICFGNode();
            
            if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(iNode)) {
                const Instruction* callInst = callNode->getCallSite();
                LLVMContext& context = callInst->getContext();
                MDNode* mdNode = MDNode::get(context, MDString::get(context, "rawptr_arg"));
                const_cast<Instruction*>(callInst)->setMetadata("rawptr_arg", mdNode);
                if(rawptrArgs.find(callInst) == rawptrArgs.end()) {
                    rawptrArgs.insert(callInst);
                    break;
                }

                // std::string func_name = node->getFun()->getLLVMFun()->getName().str();
                // if(rawptrArgs.find(func_name) == rawptrArgs.end()) {
                //     rawptrArgs.insert(func_name);
                //     break;
                // }
            }
        }

        for (SVFGNode::const_iterator it = node->OutEdgeBegin(), eit = node->OutEdgeEnd(); it != eit; ++it) {
            const SVFGEdge* edge = *it;
            const SVFGNode* succNode = edge->getDstNode();
            if (visited.find(edge) != visited.end())
                continue;

            visited.insert(edge);
            worklist.push(succNode);
        }
    }

    return rawptrArgs;
}



std::set<const SVFGNode*> S2FuzzRawPtrInstructions(Module* llvmModule, const SVFG* svfg, PTACallGraph* callgraph, std::set<const ICFGNode*>& optimized_toggle_points, std::set<const llvm::Function *>& reachable_functions, std::set<const llvm::Function *>& outermostFunctions, std::set<std::string>& targetCallSites, 
                                                        std::set<const llvm::Function *>& userDefinedFunctions)
{
    std::set<const SVFGNode*> raw_allocation_site;
    std::set<const Instruction*> raws;

    for (auto &function : *llvmModule) {
        llvm::Function *func = &function;

        /* Check whether the function is reachable or not from Fuzz Entry Point */
        if(reachable_functions.find(func) == reachable_functions.end()) {
            continue;
        }

        /* User Defined Function에 존재하는 rawptr이 아닐 경우 분석을 진행하지 않는다. */
        // if (userDefinedFunctions.find(func) == userDefinedFunctions.end()) {
        //     continue;
        // }

        std::set<const SVFGEdge*> visited;

        for (auto &bb : function) {
            for (auto &inst : bb) {
                if(inst.getMetadata("rawptr")) {
                    
                    // If rawptr metadata instruction is allocation site itself
                    if(const AllocaInst* AI = dyn_cast<AllocaInst>(&inst))
                    {
                        const SVFGNode* sNode = *((svfg->fromValue(AI)).begin());
                        if(sNode != nullptr && !isBlacklisted(sNode->getFun()->getLLVMFun())) {
                            raw_allocation_site.insert(sNode);
                        }
                    }
                    
                    // If rawptr metadata instruction is not allocation site instruction
                    else {
                        if(raws.find(&inst) == raws.end()) {
                            raws.insert(&inst);

                            // if(outermostFunctions.find(func) == outermostFunctions.end() || userDefinedFunctions.find(func) == userDefinedFunctions.end()) {
                            //     continue;
                            // }
                            const SVFGNode* sNode = *(svfg->fromValue(&inst).begin());
                            if(sNode) {
                                // std::set<std::string> rawptrArgsFuncs = processRawPtrSVFGNode(sNode, visited);
                                // for(auto i = rawptrArgsFuncs.begin(); i != rawptrArgsFuncs.end(); i++) {
                                //     targetCallSites.insert(*i);
                                // }

                                std::set<const Instruction*> rawptrArgs = processRawPtrSVFGNode(sNode, visited);
                                for(auto i = rawptrArgs.begin(); i != rawptrArgs.end(); i++) {
                                    optimized_toggle_points.insert(svfg->getPTA()->getICFG()->getBlockICFGNode(*i));
                                }

                                

                            }
                            
                        }
                    }

                }

                else if(inst.getMetadata("rawptr_deref")) {
                    if(raws.find(&inst) == raws.end()) {
                        raws.insert(&inst);
                        optimized_toggle_points.insert(svfg->getPTA()->getICFG()->getBlockICFGNode(&inst));
                    }
                }
                
                // contains unsafe block as fuzzing target
                // else if (inst.getMetadata("ydy.unsafe")) {
                //     if(llvm::isa<llvm::LoadInst>(&inst) || llvm::isa<llvm::StoreInst>(&inst)) {
                //         if(raws.find(&inst) == raws.end()) {
                //             raws.insert(&inst);
                //             optimized_toggle_points.insert(svfg->getPTA()->getICFG()->getBlockICFGNode(&inst));
                //         }
                //     }
                // }

            }
        }


    }
    std::cout << "\n ******Finished Iterating through all instructions******\n";

    // Track Allocation Sites from raw pointer instructions
    std::set<const SVFGEdge*> visited;
    for(auto a = raws.begin(); a!= raws.end(); a++)
    {
        const Instruction* inst = *a;
        std::set<const SVFGNode*> instructionSet = TrackAllocSites(inst, visited, svfg, callgraph);
        for(auto i = instructionSet.begin(); i != instructionSet.end(); i++)
        {
            const SVFGNode* topObj = *i;
            if(raw_allocation_site.find(topObj) == raw_allocation_site.end())
            {
                raw_allocation_site.insert(topObj);
            }
        }       
    }

    std::cout << "\n ******Finished Tracking Allocation Sites******\n";

    return raw_allocation_site;

}

// /* Real S2FuzzBackwardAnalysis */
// std::set<const ICFGNode*> S2FuzzBackwardAnalysis(const ICFGNode* iNode, PTACallGraph* callgraph, const Function* entryFunction)
// {
//     std::stack<std::pair<const ICFGNode*, std::vector<const ICFGNode*>>> worklist;
//     std::set<const ICFGEdge*> visited;
//     worklist.push({iNode, {iNode}});
//     const SVFFunction* targetFun = iNode->getFun();
//     std::set<const ICFGNode*> reachableiNodes;



//     int a = 0;
//     while(!worklist.empty())
//     {

//         std::pair<const ICFGNode*, std::vector<const ICFGNode*>> topElem = worklist.top();
//         const ICFGNode* vNode = topElem.first;
//         std::vector<const ICFGNode*> &path = topElem.second;
//         worklist.pop();

//         // Handle DFS path when reaching the Fuzz Entry Point
//         /* To do :
//             ,,,
//             ,,,
//         */
//         const SVFFunction* vFun = vNode->getFun();
//         if(vFun) {
//             if(vFun->getLLVMFun() == entryFunction) {

//                 const BasicBlock &entryBB = entryFunction->getEntryBlock();
//                 const BasicBlock* BB = vNode->getBB();
//                 if(BB == &entryBB) {
//                     a++;

//                     for(auto p = path.begin(); p!= path.end(); p++) {
//                         if(reachableiNodes.find(*p) == reachableiNodes.end()) {
//                             reachableiNodes.insert(*p);
//                         }
                        
//                     }
//                     // continue;
//                 }

//             }
//         }


        
//         for(ICFGNode::const_iterator it = vNode->InEdgeBegin(), eit = vNode->InEdgeEnd();
//             it != eit; ++it) {
//             ICFGEdge* edge = *it;
//             ICFGNode* prevNode = edge->getSrcNode();
//             const SVFFunction* prevFun = prevNode->getFun();
//             // const Function* stdlibfunc;

//             if(visited.find(edge) == visited.end()) {

//                 /* Implement Context-Sensitive Analysis here*/
//                 if(CallCFGEdge* dirCall = SVFUtil::dyn_cast<CallCFGEdge>(edge)) {

//                     if(prevFun) {
//                          if(!isBlacklisted(prevFun->getLLVMFun())) {
//                                 std::vector<const ICFGNode*> newPath = path;
//                                 newPath.push_back(prevNode);
//                                 worklist.push({prevNode, newPath});                         
//                             }
//                     }
                    

                    
//                 }

//                 /* Implement Rust Standard Library Blacklist here */
//                 else if (RetCFGEdge* retCall = SVFUtil::dyn_cast<RetCFGEdge>(edge)) {
//                     if(prevFun) {
//                         const Function* candidate = prevFun->getLLVMFun();
//                         if(isBlacklisted(candidate)) {
//                             // If retEdge is from Rust Standard Library, it should be not analyzed but contained to Basic Block
//                             // if(RustStdLib.find(candidate) == RustStdLib.end()) {
//                             //     RustStdLib.insert(candidate);
//                             // }
//                         }

//                         else {
//                             std::vector<const ICFGNode*> newPath = path;
//                             newPath.push_back(prevNode);
//                             worklist.push({prevNode, newPath});
//                         }
//                     }
//                 }


//                 else {

//                     std::vector<const ICFGNode*> newPath = path;
//                     newPath.push_back(prevNode);
//                     worklist.push({prevNode, newPath});
//                 }

//                 visited.insert(edge);

//             }
//         }

//     }
//     return reachableiNodes;
// }

std::set<const ICFGNode*> S2FuzzBackwardAnalysis(const ICFGNode* iNode, PTACallGraph* callgraph, const Function* entryFunction, std::set<const Function*>& RustStdLib)
{
    // ofstream backward;
    // backward.open("backward.txt", ios::out | ios::app);
    // backward << "[S2FuzzBackwardAnalysis] Target Info : " << iNode->toString() << "\n";

    FIFOWorkList<const ICFGNode*> worklist;
    std::set<const ICFGEdge*> visited;
    worklist.push(iNode);
    const SVFFunction* targetFun = iNode->getFun();
    std::set<const ICFGNode*> reachableiNodes;
    reachableiNodes.insert(iNode);

    int a = 0;
    while(!worklist.empty())
    {
        const ICFGNode* vNode = worklist.pop();

        const SVFFunction* vNodeFun = vNode->getFun();
        if(!vNodeFun) continue;
        const Function* vFun = vNodeFun->getLLVMFun();
        // backward << "\n\n";

        // backward << "[Function] : " << vFun->getName().str() << " [BB] : " << vNode->getBB()->getName().str() << "\n";

        // if(SVFUtil::isa<CallBlockNode>(vNode)) {
        //     for (ICFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd(); it != eit; ++it) {
        //         ICFGEdge* libEdge = *it;
        //         if (CallCFGEdge* dirCall = SVFUtil::dyn_cast<CallCFGEdge>(libEdge)) {
        //             ICFGNode* libNode = libEdge->getDstNode();
        //             if(!libNode->hasOutgoingEdge()) {
        //                 const SVFFunction* prevFun = libNode->getFun();
        //                 if(!prevFun) continue;
        //                 const Function* candidate = prevFun->getLLVMFun();
        //                 if(isBlacklisted(candidate) || isDroplisted(candidate)) {
        //                     RustStdLib.insert(candidate);
        //                 }
        //             }
        //         }
        //     }
        // }

        



        
        for(ICFGNode::const_iterator it = vNode->InEdgeBegin(), eit = vNode->InEdgeEnd();
            it != eit; ++it) {

            ICFGEdge* edge = *it;
            if (visited.find(edge) != visited.end()) continue;
            ICFGNode* prevNode = edge->getSrcNode();
            const SVFFunction* prevFun = prevNode->getFun();
            if(!prevFun) continue;
            const Function* candidate = prevFun->getLLVMFun();


            // if(CallCFGEdge* dirCall = SVFUtil::dyn_cast<CallCFGEdge>(edge)) {
            //     backward << "CallCFGEdge : " << edge->toString() << "\n";
            //     if(prevFun && !(isBlacklisted(prevFun->getLLVMFun()) || isDroplisted(prevFun->getLLVMFun()))) {
            //         worklist.push(prevNode);                         
            //         reachableiNodes.insert(prevNode);
            //     }
            // }

            if(RetCFGEdge* retCall = SVFUtil::dyn_cast<RetCFGEdge>(edge)) {
                // backward << "RetCFGEdge : " << edge->toString() << "\n";

                if(isBlacklisted(candidate) || isDroplisted(candidate)) {
                    // recheck
                    RustStdLib.insert(candidate);
                } else {
                    worklist.push(prevNode);
                    reachableiNodes.insert(prevNode);
                }
            
            }


            else if(CallCFGEdge* dirCall = SVFUtil::dyn_cast<CallCFGEdge>(edge)) {
                if(isReachableForRust(prevFun, targetFun, callgraph)) {
                    worklist.push(prevNode);
                    reachableiNodes.insert(prevNode);
                }
            }

            else {

                worklist.push(prevNode);
                reachableiNodes.insert(prevNode);
            }

            visited.insert(edge);

        }

    }

    return reachableiNodes;

}

// /* S2FuzzBackwardAnalysis with Pruning Rate Information */
// std::set<const ICFGNode*> S2FuzzBackwardAnalysis_with_pruning_info(const ICFGNode* iNode, PTACallGraph* callgraph, std::set<const Function*>& RustStdLib, const Function* entryFunction)
// {
//     std::queue<std::tuple<const ICFGNode*, std::vector<const ICFGNode*>, std::set<const Function*>>> worklist;
//     std::set<const ICFGEdge*> visited;
//     worklist.push({iNode, {iNode}, {}});
//     const SVFFunction* targetFun = iNode->getFun();
//     std::set<const ICFGNode*> reachableiNodes;


//     int a = 0;
//     while(!worklist.empty())
//     {
//         auto topElem = worklist.front();
//         worklist.pop();
//         const ICFGNode* vNode = std::get<0>(topElem);
//         std::vector<const ICFGNode*> path = std::get<1>(topElem);
//         std::set<const Function*> tempRustStdLib = std::get<2>(topElem);

//         // Handle DFS path when reaching the Fuzz Entry Point
//         /* To do :
//             ,,,
//             ,,,
//         */
//         const SVFFunction* vFun = vNode->getFun();

//         if(vFun && vFun->getLLVMFun() == entryFunction) {
            
//             const BasicBlock &entryBB = entryFunction->getEntryBlock();
//             const BasicBlock* BB = vNode->getBB();
//             if(BB == &entryBB) {
//                 a++;
//                 for(auto p = path.begin(); p!= path.end(); p++) {
//                     if(reachableiNodes.find(*p) == reachableiNodes.end()) {
//                         reachableiNodes.insert(*p);
//                     }
//                 }
//                 RustStdLib.insert(tempRustStdLib.begin(), tempRustStdLib.end());
//             }

            
//         }

//         // First handle the RetEdge to check whether Standard Library Functions do exist or not
//         for (ICFGNode::const_iterator it = vNode->InEdgeBegin(), eit = vNode->InEdgeEnd(); it != eit; ++it) {
//             ICFGEdge* edge = *it;
//             ICFGNode* prevNode = edge->getSrcNode();
//             const SVFFunction* prevFun = prevNode->getFun();
//             const Function* candidate;

//             if (RetCFGEdge* retCall = SVFUtil::dyn_cast<RetCFGEdge>(edge)) {
//                 if (prevFun) {
//                     candidate = prevFun->getLLVMFun();
//                     if (isBlacklisted(candidate)) {
//                         tempRustStdLib.insert(candidate);
//                     }
//                 }
//             }

//         }
        
//         for(ICFGNode::const_iterator it = vNode->InEdgeBegin(), eit = vNode->InEdgeEnd();
//             it != eit; ++it) {
//             ICFGEdge* edge = *it;
//             if (visited.find(edge) != visited.end()) continue;

//             ICFGNode* prevNode = edge->getSrcNode();
//             const SVFFunction* prevFun = prevNode->getFun();

//             if(CallCFGEdge* dirCall = SVFUtil::dyn_cast<CallCFGEdge>(edge)) {
//                 if(prevFun && !isBlacklisted(prevFun->getLLVMFun())) {
//                     std::vector<const ICFGNode*> newPath = path;
//                     newPath.push_back(prevNode);
//                     worklist.push({prevNode, newPath, tempRustStdLib});    
//                 }
//             }

//             else if (RetCFGEdge* retCall = SVFUtil::dyn_cast<RetCFGEdge>(edge)) {
//                 if(prevFun && !isBlacklisted(prevFun->getLLVMFun())) {
//                     std::vector<const ICFGNode*> newPath = path;
//                     newPath.push_back(prevNode);
//                     worklist.push({prevNode, newPath, tempRustStdLib});
//                 }
//             }

//             else {

//                 std::vector<const ICFGNode*> newPath = path;
//                 newPath.push_back(prevNode);
//                 worklist.push({prevNode, newPath, tempRustStdLib});
//             }

//             visited.insert(edge);

//         }

//     }

//     // myfile.close();
//     return reachableiNodes;

// }

std::set<const ICFGNode*> S2FuzzBackwardAnalysis_with_pruning_info(const ICFGNode* iNode, PTACallGraph* callgraph, const Function* entryFunction)
{
    FIFOWorkList<const ICFGNode*> worklist;
    std::set<const ICFGEdge*> visited;
    worklist.push(iNode);
    const SVFFunction* targetFun = iNode->getFun();
    std::set<const ICFGNode*> reachableiNodes;
    reachableiNodes.insert(iNode);

    int a = 0;
    while(!worklist.empty())
    {
       const ICFGNode* vNode = worklist.pop();

        const SVFFunction* vFun = vNode->getFun();
        
        for(ICFGNode::const_iterator it = vNode->InEdgeBegin(), eit = vNode->InEdgeEnd();
            it != eit; ++it) {
            ICFGEdge* edge = *it;
            if (visited.find(edge) != visited.end()) continue;

            ICFGNode* prevNode = edge->getSrcNode();
            const SVFFunction* prevFun = prevNode->getFun();

            if(CallCFGEdge* dirCall = SVFUtil::dyn_cast<CallCFGEdge>(edge)) {
                if(prevFun && !isDroplisted(prevFun->getLLVMFun())) {
                    worklist.push(prevNode);                         
                    reachableiNodes.insert(prevNode);
                }
            }

            else if(RetCFGEdge* retCall = SVFUtil::dyn_cast<RetCFGEdge>(edge)) {
                if(prevFun) {
                    const Function* candidate = prevFun->getLLVMFun();
                    if(isDroplisted(candidate)) {
                        // RustStdLib.insert(candidate);
                    } else {
                        worklist.push(prevNode);
                        reachableiNodes.insert(prevNode);
                    }
                }
            }

            else {

                worklist.push(prevNode);
                reachableiNodes.insert(prevNode);
            }

            visited.insert(edge);

        }

    }

    return reachableiNodes;

}


void processHeapTargets(const std::set<const SVFGNode*>& heapObjs, std::set<const ICFGNode*>& toggleHeapNode, std::set<const SVFGEdge*>& heapvisited, std::mutex &mtx, std::set<const llvm::Function *> reachableFunctions) {
    for(auto hi = heapObjs.begin(); hi != heapObjs.end(); hi++)
    {
        const SVFGNode* heapObj = *hi;
        MarkHeapAllocationSites(heapObj);        
        ParallelS2FuzzHeapAccessSites(heapObj, toggleHeapNode, heapvisited, mtx, reachableFunctions);
    }

}


void processStackTargets(const std::set<const SVFGNode*>& stackObjs, std::set<const ICFGNode*>& toggleStackNode, std::set<const SVFGEdge*>& stackvisited, std::mutex &mtx) {
    for(auto si = stackObjs.begin(); si != stackObjs.end(); si++)
    {
        const SVFGNode* stackObj = *si;
        MarkStackAllocationSites(stackObj);
        ParallelS2FuzzStackAccessSites(stackObj, toggleStackNode, stackvisited, mtx);
    }
}



void S2FuzzStaticAnalysis(SVFModule* svfModule, Module* module, const SVFG* svfg, ICFG* icfg, PTACallGraph* callgraph, size_t fuzzing_options, bool debug_pruning_rate, int allbbcount, std::set<const BasicBlock*>& allBasicBlocks)
{
    // StaticAnalysis Debugging
    ofstream myfile;
    myfile.open("S2Fuzz.txt", ios::out | ios::trunc);

    ofstream lineInfo;

    ofstream ToggleInfo;
    ToggleInfo.open("ToggleInfo.txt", ios::out | ios::app);

    if(debug_pruning_rate) {
        std::cout << "\n ****** Static Analysis with Pruning Rate Information  ******\n";
    }
    else {
        std::cout << "\n ****** Static Analysis without Pruning Rate Information (for static time evaluation) ******\n";
    }
    


    /* Get all reachable functions from the Fuzz Entry Function */
    std::set<const llvm::Function *> reachableFunctions;
    const Function* mainFuzzEntry;
    /* fuzzing_options == 0 --> afl.rs */
    if(fuzzing_options == 0) {
        mainFuzzEntry = get_function(svfModule);
        const BasicBlock& mainBB = mainFuzzEntry->front();
        const Instruction *mainInst = &mainBB.front();
        const ICFGNode* mainNode = icfg->getBlockICFGNode(mainInst);

        const SVFFunction* svfMain = svfModule->getSVFFunction(mainFuzzEntry);
        PTACallGraphNode* startNode = callgraph->getCallGraphNode(svfMain);
        reachableFunctions = callgraphTraverse(startNode);
        // reachableFunctions = TraverseCFGReachableFunc(mainNode);
    }

    // Reachable user defined functions reachable from main, not standard library
    std::set<const llvm::Function *> userDefinedFunctions;
    for(auto fi = reachableFunctions.begin(); fi != reachableFunctions.end(); fi++) {
        const Function* func = *fi;
        if(func->isDeclaration() || (isBlacklisted(func) && !isCMLibrarylisted(func)) || isDroplisted(func)) continue;
        userDefinedFunctions.insert(func);
        // std::cout << "User Defined Function : " << func->getName().str() << "\n";
    }


    // Reachable standard outermost functions reachable from main (these points can be target points)
    std::set<const llvm::Function *> outermostFunctions;
    for (const Function* userFunc : userDefinedFunctions) {
        
        for (const BasicBlock &BB : *userFunc) {
            for (const Instruction &I : BB) {
                if (const CallInst *callInst = dyn_cast<CallInst>(&I) ) {
                    const Function* calledFunc = callInst->getCalledFunction();
                    if(!calledFunc) continue;
                    if(reachableFunctions.find(calledFunc) == reachableFunctions.end()) continue;
                    if(userDefinedFunctions.find(calledFunc) != userDefinedFunctions.end()) continue;
                    // std::string callingFunc = calledFunc->getName().str();
                    if(outermostFunctions.find(calledFunc) == outermostFunctions.end()) {
                        outermostFunctions.insert(calledFunc);
                    }

                }

                else if (const InvokeInst *invokeInst = dyn_cast<InvokeInst>(&I)) {
                    const Function* calledFunc = invokeInst->getCalledFunction();
                    if(!calledFunc) continue;
                    if(reachableFunctions.find(calledFunc) == reachableFunctions.end()) continue;
                    if(userDefinedFunctions.find(calledFunc) != userDefinedFunctions.end()) continue;
                    if(outermostFunctions.find(calledFunc) == outermostFunctions.end()) {
                        outermostFunctions.insert(calledFunc);
                    }

                }
            }
        }
    }



    std::set<std::string> outermostFunctionsName;
    for(auto fi = outermostFunctions.begin(); fi != outermostFunctions.end(); fi++) {
        const Function* func = *fi;
        outermostFunctionsName.insert(func->getName().str());
        // std::cout << "Outermost Function : " << func->getName().str() << "\n";
    }

    

    /* fuzzing_options == 1 --> cargo-fuzz */
    if(fuzzing_options == 1) {
        mainFuzzEntry = get_function(svfModule);
        const BasicBlock& mainBB = mainFuzzEntry->front();
        const Instruction *mainInst = &mainBB.front();
        const ICFGNode* mainNode = icfg->getBlockICFGNode(mainInst);

        // const SVFFunction* svfMain = svfModule->getSVFFunction(mainFuzzEntry);
        // PTACallGraphNode* startNode = callgraph->getCallGraphNode(svfMain);
        reachableFunctions = TraverseCFGReachableFunc(mainNode);
    }

    /* Debugging Reachable Functions from the Fuzz Entry Function */
    // for(std::set<const llvm::Function *>::const_iterator it = reachableFunctions.begin(), eit = reachableFunctions.end(); it!=
    //     eit; ++it) {
        
    //     const Function* candidate = *it;
    //     myfile << "Candidate Function : " << candidate->getName().str() << "\n";

    // }

    /* Optmized Toggle Points ICFNode */
    std::set<const ICFGNode*> OptimizedTogglePoints;
    std::set<std::string> targetCallSites;

    /* Raw Pointer Load/Store -> OptimizedTogglePoints && Allocation Sites stemming from Raw Pointers -> topObjs (from only reachable functions) */
    std::set<const SVFGNode*> topObjs = S2FuzzRawPtrInstructions(module, svfg, callgraph, OptimizedTogglePoints, reachableFunctions, outermostFunctions, targetCallSites, userDefinedFunctions);
    
    // for(auto ti = topObjs.begin(); ti != topObjs.end(); ti++) {
    //     errs() << "Top Object : " << (*ti)->toString() << "\n";
    // }
    
    /* Raw Pointer Related Heap Objects */
    std::set<const SVFGNode*> heapObjs;
    std::set<const SVFGEdge*> heapvisited;

    /* Raw Pointer Related Stack Objects */
    std::set<const SVFGNode*> stackObjs;
    std::set<const SVFGEdge*> stackvisited;

    /* Toggle Points ICFNode */
    std::set<const ICFGNode*> toggleNode;

    /* Counting Stack/Heap Object related to the Raw Pointers */
    int a = 0;

    
    // /* Count All Basic Blocks from Fuzz Entry Point */
    // int allbbcount = 0;
    // std::set<const BasicBlock*> allBasicBlocks = countAllBB(svfModule, icfg, &allbbcount, fuzzing_options);
    std::set<std::string> DoubleDropFuncs;

    


    /* Measure Static Analysis Time*/
    auto start = std::chrono::high_resolution_clock::now();

    for(auto ii = topObjs.begin(); ii != topObjs.end(); ii++)
    {
        a++;
    }

    std::cout << "\n ******" << "Total Allocation Sites : " << a << "******\n";
    
    std::set<const SVFGNode*> heapNodes;
    std::set<const SVFGNode*> stackNodes;
    
    
   /* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@Before Optimization@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */ 
    for(auto ii = topObjs.begin(); ii != topObjs.end(); ii++)
    {
        const SVFGNode* topPtr = *ii;  
        HeapOrStackIdentification(topPtr, heapObjs, stackObjs, heapNodes, stackNodes);
    }
   /* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@Before Optimization@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */ 


   /* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@Before Optimization@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */ 
    for(auto hi = heapObjs.begin(); hi != heapObjs.end(); hi++)
    {
        const SVFGNode* heapObj = *hi;
        MarkHeapAllocationSites(heapObj);        
        S2FuzzHeapAccessSites(heapObj, toggleNode, heapvisited, reachableFunctions, outermostFunctionsName, &DoubleDropFuncs);
    }
    int m = 0;
    for(auto &n : toggleNode) {
        m++;
    }
    std::cout << "\n ******Total heap toggle Nodes : " << m << "******\n";

    std::cout << "\n ******Finished Marking Heap Access Sites******\n";

    for(auto si = stackObjs.begin(); si != stackObjs.end(); si++)
    {
        const SVFGNode* stackObj = *si;
        MarkStackAllocationSites(stackObj);
        S2FuzzStackAccessSites(stackObj, toggleNode, stackvisited);
    }

    m = 0;
    for(auto &tn : toggleNode) {
        m++;
    }
    std::cout << "\n ******Total toggle Nodes : " << m << "******\n";

    int l = 0;
    for(auto & ho : heapObjs) {
        l++;
    }
    std::cout << "\n ******Total Heap Allocation Sites : " << l << "******\n";
    l = 0;
    for(auto & so : stackObjs) {
        l++;
    }
    std::cout << "\n ******Total Stack Allocation Sites : " << l << "******\n";

    std::cout << "\n ******Finished Marking Stack Access Sites******\n";
    

    for(auto ti = toggleNode.begin(); ti != toggleNode.end(); ti++)
    {
        
        const ICFGNode* iNode = *ti;
        const SVFFunction* svfFun = iNode->getFun();
        if(svfFun) {
            const Function* func = iNode->getFun()->getLLVMFun();
            // If the function is not in the reachable functions, then it is not a toggle point
            if(reachableFunctions.find(func) == reachableFunctions.end()) continue;
            
            // // If the function is in the standard library, then it should be performed with outermost functions
            if(userDefinedFunctions.find(func) == userDefinedFunctions.end())
            {
                if (targetCallSites.find(func->getName().str()) == targetCallSites.end()) {
                    targetCallSites.insert(func->getName().str());
                }
            }

            // if(outermostFunctions.find(func) != outermostFunctions.end() && targetCallSites.find(func->getName().str()) == targetCallSites.end()) {
            //     targetCallSites.insert(func->getName().str());
            // }

            // Else if the fnction is in the user defined functions, then it is a toggle point directly
            else {
                if(OptimizedTogglePoints.find(iNode) == OptimizedTogglePoints.end()) {
                    OptimizedTogglePoints.insert(iNode);
                }
            }

        }
        
    }


    for (auto ti = DoubleDropFuncs.begin(); ti != DoubleDropFuncs.end(); ti++) {
        if (targetCallSites.find(*ti) == targetCallSites.end()) {
            targetCallSites.insert(*ti);
        }
    }


    for (const Function* userFunc : userDefinedFunctions) {
        for (const BasicBlock &BB : *userFunc) {
            for (const Instruction &I : BB) {
                if (const CallInst *callInst = dyn_cast<CallInst>(&I) ) {
                    const Function* calledFunc = callInst->getCalledFunction();
                    if(!calledFunc) continue;
                    std::string callingFunc = calledFunc->getName().str();
                    if(targetCallSites.find(callingFunc) != targetCallSites.end()) {

                        Instruction *inst = const_cast<Instruction*>(&I);
                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "stdCallSite");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("stdCallSite", node);

                        const ICFGNode* iNode = icfg->getBlockICFGNode(&I);
                        if (iNode) {
                            OptimizedTogglePoints.insert(iNode);
                        }
                    }

                }

                else if (const InvokeInst *invokeInst = dyn_cast<InvokeInst>(&I)) {
                    const Function* calledFunc = invokeInst->getCalledFunction();
                    if(!calledFunc) continue;
                    std::string callingFunc = calledFunc->getName().str();
                    if(targetCallSites.find(callingFunc) != targetCallSites.end()) {

                        Instruction *inst = const_cast<Instruction*>(&I);
                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "stdCallSite");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("stdCallSite", node);

                        const ICFGNode* iNode = icfg->getBlockICFGNode(&I);
                        if (iNode) {
                            OptimizedTogglePoints.insert(iNode);
                        }
                    }
                }


            }
        }
    }

    // std::set<const SVF::ICFGNode *> dominatedTargetPoints;

    // std::map<const Function*, std::vector<const BasicBlock*>> functionToBBs;


    // for(auto ti = OptimizedTogglePoints.begin(); ti != OptimizedTogglePoints.end(); ti++) {
    //     a++;
    //     const ICFGNode* target = *ti;
    //     const SVFFunction* svfFun = target->getFun();
    //     if(svfFun) {
    //         const Function* func = target->getFun()->getLLVMFun();
    //         functionToBBs[func].push_back(target->getBB());
    //     }
    // }

    // for(auto& entry : functionToBBs) {
    //     const Function* func = entry.first;
    //     const std::vector<const BasicBlock*>& bbs = entry.second;
        
    //     if (func->empty()) continue;

    //     DominatorTree DT;
    //     DT.recalculate(const_cast<Function&>(*func));

    //     std::set<const BasicBlock*> dominatedBBs;

    //     for(size_t i = 0; i < bbs.size(); i++) {
    //         for (size_t j = i + 1; j < bbs.size(); j++) {
    //             if (i == j) continue;

    //             const BasicBlock* bb1 = bbs[i];
    //             const BasicBlock* bb2 = bbs[j];

    //             if (DT.dominates(bb1, bb2)) {
    //                 dominatedBBs.insert(bb2);
    //             }
    //         }
    //     }

    //     for (const BasicBlock* bb : dominatedBBs) {
    //         for (const Instruction& I : *bb) {
    //             const ICFGNode* iNode = icfg->getBlockICFGNode(&I);
    //             if (iNode) {
    //                 dominatedTargetPoints.insert(iNode);
    //             }
    //         }
    //     }

        
    // }

    // a = 0;
    // for (const ICFGNode* node : dominatedTargetPoints) {
    //     a++;
    // }

    // if (a == 0) {
    //     dominatedTargetPoints = OptimizedTogglePoints;
    //     a = OptimizedTogglePoints.size();
    // }
    // ofstream stdfuncs;
    // stdfuncs.open("stdfuncs.txt", ios::out | ios::trunc);
    // for(auto i = OptimizedTogglePoints.begin(); i != OptimizedTogglePoints.end(); i++) {
    //     optToggle << (*i)->toString() << "\n\n\n\n";
    // }
    // optToggle.close();

    int z = 0;
    for(auto i = OptimizedTogglePoints.begin(); i != OptimizedTogglePoints.end(); i++) {
        z++;
    }
    std::cout << "\n ******Total Optimized Toggle Points : " << z << "******\n";


   /* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@Before Optimization@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */ 


   /* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@Before Optimization@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */ 
    int b = 0;

    std::set<const BasicBlock*> withinSameBB;
    // std::set<const Function*> withinSameFunc;
    int total_reachable = 0;

    std::map<int, ToggleData> ToggleBasic;

    std::cout <<"\n ****** Start Backward Reachability Analysis ******\n";
    for(auto ti = OptimizedTogglePoints.begin(); ti != OptimizedTogglePoints.end(); ti++)
    {
        const ICFGNode* target = *ti;
        // const ICFGNode* orig_target = *ti;

        /* Conduct Backward Reachability to each toggle Target */
        std::set<const Function*> RustStdLibFunc;
        std::set<const ICFGNode*> checked_iNodes;
        int reachable = 0;
        std::set<const BasicBlock*> ToggleBB;
        std::set<const BasicBlock*> ToggleSTDBB;
        // std::cout << "Target : " << target->toString() << "\n";


        const SVFFunction* svfFun = target->getFun();
        if(svfFun) {

            const Function* func = target->getFun()->getLLVMFun();
            if(isDroplisted(func)) continue;
            // if(isBlacklisted(func) && !isCMLibrarylisted(func)) continue;
            // std::cout << "Target Function : " << func->getName().str() << "\n";
            
            
            
             
            
        
            
        //     if(withinSameFunc.find(func) != withinSameFunc.end()) {
        //         myfile << "\n";
        //         myfile << "**Target is already analyzed**" << target->toString() << " is in the " << func->getName().str() << "\n";
        //         continue;
        //     }

        //     withinSameFunc.insert(func);


            for (auto &BB : *func) {
                if(allBasicBlocks.find(&BB)!= allBasicBlocks.end()) { 
                    if(ToggleBB.find(&BB) == ToggleBB.end()) {
                        // reachable++;
                        ToggleBB.insert(&BB);
                    }
                }
            }

        //     // const BasicBlock& lastBB = func->back();
        //     // if(lastBB.empty()) continue;
        //     // const Instruction& lastInst = lastBB.back();

        //     // target = icfg->getBlockICFGNode(&lastInst);

            
        }

        // /* If the toggle point is in the same basic block, skip analyzing */
        const BasicBlock* targetBB = target->getBB();
        if(targetBB) {
            if(withinSameBB.find(targetBB) != withinSameBB.end()) continue;
            withinSameBB.insert(targetBB);
        }

        


        /* If debug_pruning_rate is true, we need to compute backward reachability basic block ratio with std library basic block (S2FuzzBackwardAnalysis_with_pruning_info)
           Otherwise, it just conducts backward reachability analysis without actually compute basic block ratio (S2FuzzBackwardAnalysis)
        */

        if(debug_pruning_rate) {
            checked_iNodes = S2FuzzBackwardAnalysis_with_pruning_info(target, callgraph, mainFuzzEntry);
            //recheck
            // for(auto fi = RustStdLibFunc.begin(); fi!= RustStdLibFunc.end(); fi++) {
            //     const Function* func = *fi;
            //     for (auto &BB : *func) {
            //         if(allBasicBlocks.find(&BB)!= allBasicBlocks.end()) { 
            //             if(ToggleSTDBB.find(&BB) == ToggleSTDBB.end()) {
            //                 reachable++;
            //                 ToggleSTDBB.insert(&BB);
            //             }
            //         }
                    
            //     }

            // }
        }


        else {
            
            // if (target->getId() == 11005) {
            //     checked_iNodes = S2FuzzBackwardAnalysis(target, callgraph, mainFuzzEntry, RustStdLibFunc);
            // }
            checked_iNodes = S2FuzzBackwardAnalysis(target, callgraph, mainFuzzEntry, RustStdLibFunc);
            //recheck
            for (auto fi = RustStdLibFunc.begin(); fi != RustStdLibFunc.end(); fi++) {
                const Function* func = *fi;
                const SVFFunction* svfFunc = svfModule->getSVFFunction(func);
                if(svfFunc) {
                    // stdfuncs << "Meet Returned Std Function : "<< func->getName().str() << "\n\n";
                    PTACallGraphNode* callgraphNode = callgraph->getCallGraphNode(svfFunc);
                    if(callgraphNode) {
                        std::set<Function*> functions = traverseSTDfuncs(callgraphNode);
                        int i = 0;
                        for (auto& fun : functions) {
                            if(isBlacklisted(fun) || isDroplisted(fun)) continue;
                            i++;
                            // stdfuncs << "[" << i << "] : Inside Std Function : " << fun->getName().str() << "\n";
                            for (auto &BB : *fun) {
                                if(allBasicBlocks.find(&BB)!= allBasicBlocks.end()) {
                                    if(ToggleBB.find(&BB) == ToggleBB.end()) {
                                        ToggleBB.insert(&BB);
                                    }
                                }
                            }
                        }
                        // stdfuncs << "\n\n";
                    }
                }
            }



            for(auto fi = RustStdLibFunc.begin(); fi!= RustStdLibFunc.end(); fi++) {
                const Function* func = *fi;
                for (auto &BB : *func) {
                    if(allBasicBlocks.find(&BB)!= allBasicBlocks.end()) { 
                        if(ToggleSTDBB.find(&BB) == ToggleSTDBB.end()) {
                            reachable++;
                            ToggleSTDBB.insert(&BB);
                        }
                    }
                    
                }

            }
        }


        /* If we cannot find any reachable path from target points, we skip further analysis */
        if(checked_iNodes.empty()) continue;
            


        std::string lineInfoStr;
        /* Get Line Info */
        if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(target))
        {
            Instruction* I = const_cast<Instruction*>(intra->getInst());
            if (DILocation *Loc = I->getDebugLoc()) { 
                unsigned Line = Loc->getLine();
                StringRef File = Loc->getFilename();
                StringRef Directory = Loc->getDirectory();
                
                lineInfoStr = " (File: " + Directory.str() + "/" + File.str() + ", Line: " + std::to_string(Line) + ")";
            }  
            else {
                lineInfoStr = " (" + std::string("File: ") + "Unknown" + ", Line: " + "Unknown)";
            }  
        }
        else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(target))
        {
            Instruction* I = const_cast<Instruction*>(callNode->getCallSite());
            if (DILocation *Loc = I->getDebugLoc()) { 
                unsigned Line = Loc->getLine();
                StringRef File = Loc->getFilename();
                StringRef Directory = Loc->getDirectory();
            
                lineInfoStr = " (File: " + Directory.str() + "/" + File.str() + ", Line: " + std::to_string(Line) + ")";
            } 
            else {
                lineInfoStr = " (" + std::string("File: ") + "Unknown" + ", Line: " + "Unknown)";
            }
        }
        else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(target))
        {
            Instruction* I = const_cast<Instruction*>(RetNode->getCallSite());
            if (DILocation *Loc = I->getDebugLoc()) { 
                unsigned Line = Loc->getLine();
                StringRef File = Loc->getFilename();
                StringRef Directory = Loc->getDirectory();
                
                lineInfoStr = std::string("File: ") + Directory.str() + "/" + File.str() + ", Line: " + std::to_string(Line);
            } 
            else {
                lineInfoStr = " (" + std::string("File: ") + "Unknown" + ", Line: " + "Unknown)";
            }
        }
        else {
            lineInfoStr = " (" + std::string("File: ") + "Unknown" + ", Line: " + "Unknown)";
        }
        ToggleBasic[b].lineInfo = lineInfoStr;
        // if(ToggleBasic[b].lineInfo == " (" + std::string("File: ") + "Unknown" + ", Line: " + "Unknown)") continue;
        /* Get Line Info */


        ToggleInfo << "[" << b <<"] : ";
        

        for(auto ci = checked_iNodes.begin(); ci != checked_iNodes.end(); ci++){
            const BasicBlock* bb = (*ci)->getBB();            

            /* Write Reachable Basic Block Unique ID */
            if(bb) {
                // if(allBasicBlocks.find(bb)!= allBasicBlocks.end()) {
                    if(ToggleBB.find(bb) == ToggleBB.end()) {
                        reachable++;
                        ToggleBB.insert(bb);
                    }

                // }
            }

        }
        // myfile << "[Toggle Point " << b <<"] : " << target->toString() << "\n";
        // myfile << "Reachable BB : " << reachable << "\n\n";

        for(auto &bb : ToggleBB) {

            for (auto &I : *bb) {
                if (auto *CI = dyn_cast<CallInst>(&I)) {
                    if(MDNode *MD = CI->getMetadata("bb_index")) {
                        if (llvm::Metadata* md = MD->getOperand(0)) {
                            if (auto* mdv = llvm::dyn_cast<llvm::ConstantAsMetadata>(md)) {
                                if (llvm::ConstantInt* ci = llvm::dyn_cast<llvm::ConstantInt>(mdv->getValue())) {
                                    uint64_t bbIndexValue = ci->getZExtValue();
                                    ToggleInfo << bbIndexValue << " ";
                                    ToggleBasic[b].BBIds.insert(bbIndexValue);
                                }

                            }
                        }
                    }
                }
            }
        }

        ToggleInfo << "\n";   
        total_reachable+= reachable; 

        // StaticAnalysis Debugging
        myfile << "\n\n";
        // myfile << "Orig Toggle Point [" << b << "] : " << orig_target->toString() << "\n";
        myfile << "Toggle Point [" << b << "] : " << target->toString() << "\n";
        // if(svfFun) {
        //     const Function* func = target->getFun()->getLLVMFun();
        //     myfile << "Toggle Point Function : " << func->getName().str() << "\n";  
        // }
        myfile << "Reachable BB : " << reachable << "\n\n";

        for(auto ti = ToggleBB.begin(); ti != ToggleBB.end(); ti++) {
            const BasicBlock* bb = *ti;
            if(bb) {
                myfile << "Func : " << bb->getParent()->getName().str() << " BB : " << bb->getName().str() << "\n";
            }
            
        }

        for(auto ti = ToggleSTDBB.begin(); ti != ToggleSTDBB.end(); ti++) {
            const BasicBlock* bb = *ti;
            if(bb) {
                myfile << "Func : " << bb->getParent()->getName().str() << " BB : " << bb->getName().str() << "\n";
            }
            
        }        

        b++;

    }
   /* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@Before Optimization@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */ 

    std::map<int, ToggleData> ToggleCondensed;

    std::set<std::set<uint64_t>> unique_values;
    int condensed_count = 0;

    for (const auto& pair : ToggleBasic) {
        const int& key = pair.first;
        const ToggleData& value = pair.second;

        if (unique_values.find(value.BBIds) == unique_values.end()) {
            ToggleCondensed[condensed_count++] = value;
            unique_values.insert(value.BBIds); 
        }
    }

    
    int c = 0;
    ofstream fDebug1;
    fDebug1.open("ToggleCondensed.txt", ios::out | ios::trunc);
    for(auto &ti : ToggleCondensed) {
        fDebug1 << "[" << ti.first << "] : ";
        c++;
        for(auto &bb : ti.second.BBIds) {
            fDebug1 << bb << " ";
        }
        fDebug1 << "\n";
    }
    fDebug1.close();

    ofstream fDebug0;
    fDebug0.open("ToggleCondensedLineInfo.txt", ios::out | ios::trunc);
    for(auto &ti : ToggleCondensed) {
        fDebug0 << "[" << ti.first << "] : ";
        for(auto &bb : ti.second.BBIds) {
            fDebug0 << bb << " ";
        }
        fDebug0 << ti.second.lineInfo << "\n";
    }
    fDebug0.close();
    
    std::map<int, ToggleData> ToggleRMSubset;
    int rm_count = 0;
    for (const auto& ti : ToggleCondensed) {
        bool is_subset_found = false;

        for (const auto& tj : ToggleCondensed) {
            if (ti.first != tj.first && is_subset(ti.second.BBIds, tj.second.BBIds)) {
                is_subset_found = true;
                break;
            }
        }

        if (!is_subset_found) {
            ToggleRMSubset[rm_count++] = ti.second;
        }
    }

    ofstream fDebug2;
    fDebug2.open("TogglePostDominated.txt", ios::out | ios::trunc);
    for(auto &ti : ToggleRMSubset) {
        fDebug2 << "[" << ti.first << "] : ";
        for(auto &bb : ti.second.BBIds) {
            fDebug2 << bb << " ";
        }
        fDebug2 << "\n";
    }
    fDebug2.close();

    ofstream fDebug3;
    fDebug3.open("TogglePostDominatedLineInfo.txt", ios::out | ios::trunc);
    for(auto &ti : ToggleRMSubset) {
        fDebug3 << "[" << ti.first << "] : ";
        for(auto &bb : ti.second.BBIds) {
            fDebug3 << bb << " ";
        }
        fDebug3 << ti.second.lineInfo << "\n";
    }
    fDebug3.close();


    // mergeTargets(ToggleCondensed, 0.5, 30);
    // ofstream fDebug4;
    // fDebug4.open("ToggleInfoNew.txt", ios::out | ios::trunc);
    // for(auto &ti : ToggleCondensed) {
    //     fDebug4 << "[" << ti.first << "] : ";
    //     for(auto &bb : ti.second.BBIds) {
    //         fDebug4 << bb << " ";
    //     }
    //     fDebug4 << "\n";
    // }
    // fDebug4.close();


    // ofstream fDebug5;
    // fDebug5.open("ToggleInfoNewLineInfo.txt", ios::out | ios::trunc);
    // for(auto &ti : ToggleCondensed) {
    //     fDebug5 << "[" << ti.first << "] : ";
    //     for(auto &bb : ti.second.BBIds) {
    //         fDebug5 << bb << " ";
    //     }
    //     fDebug5 << ti.second.lineInfo << "\n";
    // }
    // fDebug5.close();



    std::cout <<"\n ****** Finished Backward Reachability Analysis ******\n";

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;

    std::cout <<"\n ****** Total Static Analysis Execution Time: " << duration.count() << "******\n";
    myfile.close();
    ToggleInfo.close();


    std::cout <<"\n ****** [Optimized Toggle Points] : " << b << " ******\n";
    int average_reachable = 0;
    double pruning_rate = 0.0;
    if (b > 0) {
        average_reachable = total_reachable / b;
    }
    if (allbbcount > 0) {
        int pruned_bb = allbbcount - average_reachable;
        pruning_rate = ((double)pruned_bb / (double)allbbcount) * 100;
    }

    std::cout<<"\n ****** [All Basic Blocks] : " << allbbcount << " ******\n";

    std::cout<<"\n ****** [Average Reachable Blocks] : " << average_reachable << " ******\n";

    std::cout <<"\n ****** [Pruning Rate] : " << pruning_rate << "% ******\n";

    std::cout << "\n ****** [Final Toggle Points] : " << c << " ******\n";


    writeModuleToFile(module, "S2FuzzAnalysis.ll");
}

void S2FuzzStaticAnalysis_async_safe(SVFModule* svfModule, Module* module, const SVFG* svfg, ICFG* icfg, PTACallGraph* callgraph, size_t fuzzing_options, bool debug_pruning_rate, int allbbcount, std::set<const BasicBlock*>& allBasicBlocks)
{
    // StaticAnalysis Debugging
    ofstream myfile;
    myfile.open("S2FuzzAsyncSafe.txt", ios::out | ios::trunc);

    ofstream lineInfo;

    ofstream ToggleInfo;
    ToggleInfo.open("ToggleInfo.txt", ios::out | ios::app);

    if(debug_pruning_rate) {
        std::cout << "\n ****** Static Analysis with Pruning Rate Information  ******\n";
    }
    else {
        std::cout << "\n ****** Static Analysis without Pruning Rate Information (for static time evaluation) ******\n";
    }
    


    /* Get all reachable functions from the Fuzz Entry Function */
    std::set<const llvm::Function *> reachableFunctions;
    const Function* mainFuzzEntry;
    /* fuzzing_options == 0 --> afl.rs */
    if(fuzzing_options == 0) {
        mainFuzzEntry = get_function(svfModule);
        const BasicBlock& mainBB = mainFuzzEntry->front();
        const Instruction *mainInst = &mainBB.front();
        const ICFGNode* mainNode = icfg->getBlockICFGNode(mainInst);

        const SVFFunction* svfMain = svfModule->getSVFFunction(mainFuzzEntry);
        PTACallGraphNode* startNode = callgraph->getCallGraphNode(svfMain);
        reachableFunctions = callgraphTraverse(startNode);
        // reachableFunctions = TraverseCFGReachableFunc(mainNode);
    }

    // Reachable user defined functions reachable from main, not standard library
    std::set<const llvm::Function *> userDefinedFunctions;
    for(auto fi = reachableFunctions.begin(); fi != reachableFunctions.end(); fi++) {
        const Function* func = *fi;
        if(func->isDeclaration() || (isBlacklisted(func) && !isCMLibrarylisted(func)) || isDroplisted(func)) continue;
        userDefinedFunctions.insert(func);
        // std::cout << "User Defined Function : " << func->getName().str() << "\n";
    }


    // Reachable standard outermost functions reachable from main (these points can be target points)
    std::set<const llvm::Function *> outermostFunctions;
    for (const Function* userFunc : userDefinedFunctions) {
        
        for (const BasicBlock &BB : *userFunc) {
            for (const Instruction &I : BB) {
                if (const CallInst *callInst = dyn_cast<CallInst>(&I) ) {
                    const Function* calledFunc = callInst->getCalledFunction();
                    if(!calledFunc) continue;
                    if(reachableFunctions.find(calledFunc) == reachableFunctions.end()) continue;
                    if(userDefinedFunctions.find(calledFunc) != userDefinedFunctions.end()) continue;
                    // std::string callingFunc = calledFunc->getName().str();
                    if(outermostFunctions.find(calledFunc) == outermostFunctions.end()) {
                        outermostFunctions.insert(calledFunc);
                    }

                }

                else if (const InvokeInst *invokeInst = dyn_cast<InvokeInst>(&I)) {
                    const Function* calledFunc = invokeInst->getCalledFunction();
                    if(!calledFunc) continue;
                    if(reachableFunctions.find(calledFunc) == reachableFunctions.end()) continue;
                    if(userDefinedFunctions.find(calledFunc) != userDefinedFunctions.end()) continue;
                    if(outermostFunctions.find(calledFunc) == outermostFunctions.end()) {
                        outermostFunctions.insert(calledFunc);
                    }

                }
            }
        }
    }



    std::set<std::string> outermostFunctionsName;
    for(auto fi = outermostFunctions.begin(); fi != outermostFunctions.end(); fi++) {
        const Function* func = *fi;
        outermostFunctionsName.insert(func->getName().str());
        // std::cout << "Outermost Function : " << func->getName().str() << "\n";
    }

    

    /* fuzzing_options == 1 --> cargo-fuzz */
    if(fuzzing_options == 1) {
        mainFuzzEntry = get_function(svfModule);
        const BasicBlock& mainBB = mainFuzzEntry->front();
        const Instruction *mainInst = &mainBB.front();
        const ICFGNode* mainNode = icfg->getBlockICFGNode(mainInst);

        // const SVFFunction* svfMain = svfModule->getSVFFunction(mainFuzzEntry);
        // PTACallGraphNode* startNode = callgraph->getCallGraphNode(svfMain);
        reachableFunctions = TraverseCFGReachableFunc(mainNode);
    }

    /* Debugging Reachable Functions from the Fuzz Entry Function */
    // for(std::set<const llvm::Function *>::const_iterator it = reachableFunctions.begin(), eit = reachableFunctions.end(); it!=
    //     eit; ++it) {
        
    //     const Function* candidate = *it;
    //     myfile << "Candidate Function : " << candidate->getName().str() << "\n";

    // }

    /* Optmized Toggle Points ICFNode */
    std::set<const ICFGNode*> OptimizedTogglePoints;
    std::set<std::string> targetCallSites;

    /* Raw Pointer Load/Store -> OptimizedTogglePoints && Allocation Sites stemming from Raw Pointers -> topObjs (from only reachable functions) */
    std::set<const SVFGNode*> topObjs = S2FuzzRawPtrInstructions(module, svfg, callgraph, OptimizedTogglePoints, reachableFunctions, outermostFunctions, targetCallSites, userDefinedFunctions);
    
    // for(auto ti = topObjs.begin(); ti != topObjs.end(); ti++) {
    //     errs() << "Top Object : " << (*ti)->toString() << "\n";
    // }
    
    /* Raw Pointer Related Heap Objects */
    std::set<const SVFGNode*> heapObjs;
    std::set<const SVFGEdge*> heapvisited;

    /* Raw Pointer Related Stack Objects */
    std::set<const SVFGNode*> stackObjs;
    std::set<const SVFGEdge*> stackvisited;

    /* Toggle Points ICFNode */
    std::set<const ICFGNode*> toggleNode;

    /* Counting Stack/Heap Object related to the Raw Pointers */
    int a = 0;

    
    // /* Count All Basic Blocks from Fuzz Entry Point */
    // int allbbcount = 0;
    // std::set<const BasicBlock*> allBasicBlocks = countAllBB(svfModule, icfg, &allbbcount, fuzzing_options);
    std::set<std::string> DoubleDropFuncs;

    


    /* Measure Static Analysis Time*/
    auto start = std::chrono::high_resolution_clock::now();

    for(auto ii = topObjs.begin(); ii != topObjs.end(); ii++)
    {
        a++;
    }

    std::cout << "\n ******" << "Total Allocation Sites : " << a << "******\n";
    
    std::set<const SVFGNode*> heapNodes;
    std::set<const SVFGNode*> stackNodes;
    
    
   /* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@Before Optimization@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */ 
    for(auto ii = topObjs.begin(); ii != topObjs.end(); ii++)
    {
        const SVFGNode* topPtr = *ii;  
        HeapOrStackIdentification(topPtr, heapObjs, stackObjs, heapNodes, stackNodes);
    }
   /* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@Before Optimization@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */ 


   /* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@Before Optimization@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */ 
    for(auto hi = heapObjs.begin(); hi != heapObjs.end(); hi++)
    {
        const SVFGNode* heapObj = *hi;
        MarkHeapAllocationSites(heapObj);        
        AsyncSafeS2FuzzHeapAccessSites(heapObj, toggleNode, heapvisited, reachableFunctions, outermostFunctionsName, &DoubleDropFuncs);
    }
    int m = 0;
    for(auto &n : toggleNode) {
        m++;
    }
    std::cout << "\n ******Total heap toggle Nodes : " << m << "******\n";

    std::cout << "\n ******Finished Marking Async SafeHeap Access Sites******\n";

    for(auto si = stackObjs.begin(); si != stackObjs.end(); si++)
    {
        const SVFGNode* stackObj = *si;
        MarkStackAllocationSites(stackObj);
        AsyncSafeS2FuzzStackAccessSites(stackObj, toggleNode, stackvisited);
    }

    m = 0;
    for(auto &tn : toggleNode) {
        m++;
    }
    std::cout << "\n ******Total toggle Nodes : " << m << "******\n";

    int l = 0;
    for(auto & ho : heapObjs) {
        l++;
    }
    std::cout << "\n ******Total Heap Allocation Sites : " << l << "******\n";
    l = 0;
    for(auto & so : stackObjs) {
        l++;
    }
    std::cout << "\n ******Total Stack Allocation Sites : " << l << "******\n";

    std::cout << "\n ******Finished Marking Async Safe Stack Access Sites******\n";
    

    for(auto ti = toggleNode.begin(); ti != toggleNode.end(); ti++)
    {
        
        const ICFGNode* iNode = *ti;
        const SVFFunction* svfFun = iNode->getFun();
        if(svfFun) {
            const Function* func = iNode->getFun()->getLLVMFun();
            // If the function is not in the reachable functions, then it is not a toggle point
            if(reachableFunctions.find(func) == reachableFunctions.end()) continue;
            
            // // If the function is in the standard library, then it should be performed with outermost functions
            if(userDefinedFunctions.find(func) == userDefinedFunctions.end())
            {
                if (targetCallSites.find(func->getName().str()) == targetCallSites.end()) {
                    targetCallSites.insert(func->getName().str());
                }
            }

            // if(outermostFunctions.find(func) != outermostFunctions.end() && targetCallSites.find(func->getName().str()) == targetCallSites.end()) {
            //     targetCallSites.insert(func->getName().str());
            // }

            // Else if the fnction is in the user defined functions, then it is a toggle point directly
            else {
                if(OptimizedTogglePoints.find(iNode) == OptimizedTogglePoints.end()) {
                    OptimizedTogglePoints.insert(iNode);
                }
            }

        }
        
    }


    for (auto ti = DoubleDropFuncs.begin(); ti != DoubleDropFuncs.end(); ti++) {
        if (targetCallSites.find(*ti) == targetCallSites.end()) {
            targetCallSites.insert(*ti);
        }
    }


    for (const Function* userFunc : userDefinedFunctions) {
        for (const BasicBlock &BB : *userFunc) {
            for (const Instruction &I : BB) {
                if (const CallInst *callInst = dyn_cast<CallInst>(&I) ) {
                    const Function* calledFunc = callInst->getCalledFunction();
                    if(!calledFunc) continue;
                    std::string callingFunc = calledFunc->getName().str();
                    if(targetCallSites.find(callingFunc) != targetCallSites.end()) {

                        Instruction *inst = const_cast<Instruction*>(&I);
                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "stdCallSite");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("stdCallSite", node);

                        const ICFGNode* iNode = icfg->getBlockICFGNode(&I);
                        if (iNode) {
                            OptimizedTogglePoints.insert(iNode);
                        }
                    }

                }

                else if (const InvokeInst *invokeInst = dyn_cast<InvokeInst>(&I)) {
                    const Function* calledFunc = invokeInst->getCalledFunction();
                    if(!calledFunc) continue;
                    std::string callingFunc = calledFunc->getName().str();
                    if(targetCallSites.find(callingFunc) != targetCallSites.end()) {

                        Instruction *inst = const_cast<Instruction*>(&I);
                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "stdCallSite");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("stdCallSite", node);

                        const ICFGNode* iNode = icfg->getBlockICFGNode(&I);
                        if (iNode) {
                            OptimizedTogglePoints.insert(iNode);
                        }
                    }
                }


            }
        }
    }

    // std::set<const SVF::ICFGNode *> dominatedTargetPoints;

    // std::map<const Function*, std::vector<const BasicBlock*>> functionToBBs;


    // for(auto ti = OptimizedTogglePoints.begin(); ti != OptimizedTogglePoints.end(); ti++) {
    //     a++;
    //     const ICFGNode* target = *ti;
    //     const SVFFunction* svfFun = target->getFun();
    //     if(svfFun) {
    //         const Function* func = target->getFun()->getLLVMFun();
    //         functionToBBs[func].push_back(target->getBB());
    //     }
    // }

    // for(auto& entry : functionToBBs) {
    //     const Function* func = entry.first;
    //     const std::vector<const BasicBlock*>& bbs = entry.second;
        
    //     if (func->empty()) continue;

    //     DominatorTree DT;
    //     DT.recalculate(const_cast<Function&>(*func));

    //     std::set<const BasicBlock*> dominatedBBs;

    //     for(size_t i = 0; i < bbs.size(); i++) {
    //         for (size_t j = i + 1; j < bbs.size(); j++) {
    //             if (i == j) continue;

    //             const BasicBlock* bb1 = bbs[i];
    //             const BasicBlock* bb2 = bbs[j];

    //             if (DT.dominates(bb1, bb2)) {
    //                 dominatedBBs.insert(bb2);
    //             }
    //         }
    //     }

    //     for (const BasicBlock* bb : dominatedBBs) {
    //         for (const Instruction& I : *bb) {
    //             const ICFGNode* iNode = icfg->getBlockICFGNode(&I);
    //             if (iNode) {
    //                 dominatedTargetPoints.insert(iNode);
    //             }
    //         }
    //     }

        
    // }

    // a = 0;
    // for (const ICFGNode* node : dominatedTargetPoints) {
    //     a++;
    // }

    // if (a == 0) {
    //     dominatedTargetPoints = OptimizedTogglePoints;
    //     a = OptimizedTogglePoints.size();
    // }
    // ofstream stdfuncs;
    // stdfuncs.open("stdfuncs.txt", ios::out | ios::trunc);
    // for(auto i = OptimizedTogglePoints.begin(); i != OptimizedTogglePoints.end(); i++) {
    //     optToggle << (*i)->toString() << "\n\n\n\n";
    // }
    // optToggle.close();

    int z = 0;
    for(auto i = OptimizedTogglePoints.begin(); i != OptimizedTogglePoints.end(); i++) {
        z++;
    }
    std::cout << "\n ******Total Optimized Toggle Points : " << z << "******\n";


   /* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@Before Optimization@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */ 


   /* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@Before Optimization@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */ 
    int b = 0;

    std::set<const BasicBlock*> withinSameBB;
    // std::set<const Function*> withinSameFunc;
    int total_reachable = 0;

    std::map<int, ToggleData> ToggleBasic;

    std::cout <<"\n ****** Start Backward Reachability Analysis (Async Safe) ******\n";
    for(auto ti = OptimizedTogglePoints.begin(); ti != OptimizedTogglePoints.end(); ti++)
    {
        const ICFGNode* target = *ti;
        // const ICFGNode* orig_target = *ti;

        /* Conduct Backward Reachability to each toggle Target */
        std::set<const Function*> RustStdLibFunc;
        std::set<const ICFGNode*> checked_iNodes;
        int reachable = 0;
        std::set<const BasicBlock*> ToggleBB;
        std::set<const BasicBlock*> ToggleSTDBB;
        // std::cout << "Target : " << target->toString() << "\n";


        const SVFFunction* svfFun = target->getFun();
        if(svfFun) {

            const Function* func = target->getFun()->getLLVMFun();
            if(isDroplisted(func)) continue;
            // if(isBlacklisted(func) && !isCMLibrarylisted(func)) continue;
            // std::cout << "Target Function : " << func->getName().str() << "\n";
            
            
            
             
            
        
            
        //     if(withinSameFunc.find(func) != withinSameFunc.end()) {
        //         myfile << "\n";
        //         myfile << "**Target is already analyzed**" << target->toString() << " is in the " << func->getName().str() << "\n";
        //         continue;
        //     }

        //     withinSameFunc.insert(func);


            for (auto &BB : *func) {
                if(allBasicBlocks.find(&BB)!= allBasicBlocks.end()) { 
                    if(ToggleBB.find(&BB) == ToggleBB.end()) {
                        // reachable++;
                        ToggleBB.insert(&BB);
                    }
                }
            }

        //     // const BasicBlock& lastBB = func->back();
        //     // if(lastBB.empty()) continue;
        //     // const Instruction& lastInst = lastBB.back();

        //     // target = icfg->getBlockICFGNode(&lastInst);

            
        }

        // /* If the toggle point is in the same basic block, skip analyzing */
        const BasicBlock* targetBB = target->getBB();
        if(targetBB) {
            if(withinSameBB.find(targetBB) != withinSameBB.end()) continue;
            withinSameBB.insert(targetBB);
        }

        


        /* If debug_pruning_rate is true, we need to compute backward reachability basic block ratio with std library basic block (S2FuzzBackwardAnalysis_with_pruning_info)
           Otherwise, it just conducts backward reachability analysis without actually compute basic block ratio (S2FuzzBackwardAnalysis)
        */

        if(debug_pruning_rate) {
            checked_iNodes = S2FuzzBackwardAnalysis_with_pruning_info(target, callgraph, mainFuzzEntry);
            //recheck
            // for(auto fi = RustStdLibFunc.begin(); fi!= RustStdLibFunc.end(); fi++) {
            //     const Function* func = *fi;
            //     for (auto &BB : *func) {
            //         if(allBasicBlocks.find(&BB)!= allBasicBlocks.end()) { 
            //             if(ToggleSTDBB.find(&BB) == ToggleSTDBB.end()) {
            //                 reachable++;
            //                 ToggleSTDBB.insert(&BB);
            //             }
            //         }
                    
            //     }

            // }
        }


        else {
            
            // if (target->getId() == 11005) {
            //     checked_iNodes = S2FuzzBackwardAnalysis(target, callgraph, mainFuzzEntry, RustStdLibFunc);
            // }
            checked_iNodes = S2FuzzBackwardAnalysis(target, callgraph, mainFuzzEntry, RustStdLibFunc);
            //recheck
            for (auto fi = RustStdLibFunc.begin(); fi != RustStdLibFunc.end(); fi++) {
                const Function* func = *fi;
                const SVFFunction* svfFunc = svfModule->getSVFFunction(func);
                if(svfFunc) {
                    // stdfuncs << "Meet Returned Std Function : "<< func->getName().str() << "\n\n";
                    PTACallGraphNode* callgraphNode = callgraph->getCallGraphNode(svfFunc);
                    if(callgraphNode) {
                        std::set<Function*> functions = traverseSTDfuncs(callgraphNode);
                        int i = 0;
                        for (auto& fun : functions) {
                            if(isBlacklisted(fun) || isDroplisted(fun)) continue;
                            i++;
                            // stdfuncs << "[" << i << "] : Inside Std Function : " << fun->getName().str() << "\n";
                            for (auto &BB : *fun) {
                                if(allBasicBlocks.find(&BB)!= allBasicBlocks.end()) {
                                    if(ToggleBB.find(&BB) == ToggleBB.end()) {
                                        ToggleBB.insert(&BB);
                                    }
                                }
                            }
                        }
                        // stdfuncs << "\n\n";
                    }
                }
            }



            for(auto fi = RustStdLibFunc.begin(); fi!= RustStdLibFunc.end(); fi++) {
                const Function* func = *fi;
                for (auto &BB : *func) {
                    if(allBasicBlocks.find(&BB)!= allBasicBlocks.end()) { 
                        if(ToggleSTDBB.find(&BB) == ToggleSTDBB.end()) {
                            reachable++;
                            ToggleSTDBB.insert(&BB);
                        }
                    }
                    
                }

            }
        }


        /* If we cannot find any reachable path from target points, we skip further analysis */
        if(checked_iNodes.empty()) continue;
            


        std::string lineInfoStr;
        /* Get Line Info */
        if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(target))
        {
            Instruction* I = const_cast<Instruction*>(intra->getInst());
            if (DILocation *Loc = I->getDebugLoc()) { 
                unsigned Line = Loc->getLine();
                StringRef File = Loc->getFilename();
                StringRef Directory = Loc->getDirectory();
                
                lineInfoStr = " (File: " + Directory.str() + "/" + File.str() + ", Line: " + std::to_string(Line) + ")";
            }  
            else {
                lineInfoStr = " (" + std::string("File: ") + "Unknown" + ", Line: " + "Unknown)";
            }  
        }
        else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(target))
        {
            Instruction* I = const_cast<Instruction*>(callNode->getCallSite());
            if (DILocation *Loc = I->getDebugLoc()) { 
                unsigned Line = Loc->getLine();
                StringRef File = Loc->getFilename();
                StringRef Directory = Loc->getDirectory();
            
                lineInfoStr = " (File: " + Directory.str() + "/" + File.str() + ", Line: " + std::to_string(Line) + ")";
            } 
            else {
                lineInfoStr = " (" + std::string("File: ") + "Unknown" + ", Line: " + "Unknown)";
            }
        }
        else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(target))
        {
            Instruction* I = const_cast<Instruction*>(RetNode->getCallSite());
            if (DILocation *Loc = I->getDebugLoc()) { 
                unsigned Line = Loc->getLine();
                StringRef File = Loc->getFilename();
                StringRef Directory = Loc->getDirectory();
                
                lineInfoStr = std::string("File: ") + Directory.str() + "/" + File.str() + ", Line: " + std::to_string(Line);
            } 
            else {
                lineInfoStr = " (" + std::string("File: ") + "Unknown" + ", Line: " + "Unknown)";
            }
        }
        else {
            lineInfoStr = " (" + std::string("File: ") + "Unknown" + ", Line: " + "Unknown)";
        }
        ToggleBasic[b].lineInfo = lineInfoStr;
        // if(ToggleBasic[b].lineInfo == " (" + std::string("File: ") + "Unknown" + ", Line: " + "Unknown)") continue;
        /* Get Line Info */


        ToggleInfo << "[" << b <<"] : ";
        

        for(auto ci = checked_iNodes.begin(); ci != checked_iNodes.end(); ci++){
            const BasicBlock* bb = (*ci)->getBB();            

            /* Write Reachable Basic Block Unique ID */
            if(bb) {
                // if(allBasicBlocks.find(bb)!= allBasicBlocks.end()) {
                    if(ToggleBB.find(bb) == ToggleBB.end()) {
                        reachable++;
                        ToggleBB.insert(bb);
                    }

                // }
            }

        }
        // myfile << "[Toggle Point " << b <<"] : " << target->toString() << "\n";
        // myfile << "Reachable BB : " << reachable << "\n\n";

        for(auto &bb : ToggleBB) {

            for (auto &I : *bb) {
                if (auto *CI = dyn_cast<CallInst>(&I)) {
                    if(MDNode *MD = CI->getMetadata("bb_index")) {
                        if (llvm::Metadata* md = MD->getOperand(0)) {
                            if (auto* mdv = llvm::dyn_cast<llvm::ConstantAsMetadata>(md)) {
                                if (llvm::ConstantInt* ci = llvm::dyn_cast<llvm::ConstantInt>(mdv->getValue())) {
                                    uint64_t bbIndexValue = ci->getZExtValue();
                                    ToggleInfo << bbIndexValue << " ";
                                    ToggleBasic[b].BBIds.insert(bbIndexValue);
                                }

                            }
                        }
                    }
                }
            }
        }

        ToggleInfo << "\n";   
        total_reachable+= reachable; 

        // StaticAnalysis Debugging
        myfile << "\n\n";
        // myfile << "Orig Toggle Point [" << b << "] : " << orig_target->toString() << "\n";
        myfile << "Toggle Point [" << b << "] : " << target->toString() << "\n";
        // if(svfFun) {
        //     const Function* func = target->getFun()->getLLVMFun();
        //     myfile << "Toggle Point Function : " << func->getName().str() << "\n";  
        // }
        myfile << "Reachable BB : " << reachable << "\n\n";

        for(auto ti = ToggleBB.begin(); ti != ToggleBB.end(); ti++) {
            const BasicBlock* bb = *ti;
            if(bb) {
                myfile << "Func : " << bb->getParent()->getName().str() << " BB : " << bb->getName().str() << "\n";
            }
            
        }

        for(auto ti = ToggleSTDBB.begin(); ti != ToggleSTDBB.end(); ti++) {
            const BasicBlock* bb = *ti;
            if(bb) {
                myfile << "Func : " << bb->getParent()->getName().str() << " BB : " << bb->getName().str() << "\n";
            }
            
        }        

        b++;

    }
   /* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@Before Optimization@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */ 

    std::map<int, ToggleData> ToggleCondensed;

    std::set<std::set<uint64_t>> unique_values;
    int condensed_count = 0;

    for (const auto& pair : ToggleBasic) {
        const int& key = pair.first;
        const ToggleData& value = pair.second;

        if (unique_values.find(value.BBIds) == unique_values.end()) {
            ToggleCondensed[condensed_count++] = value;
            unique_values.insert(value.BBIds); 
        }
    }

    
    int c = 0;
    ofstream fDebug1;
    fDebug1.open("ToggleCondensed.txt", ios::out | ios::trunc);
    for(auto &ti : ToggleCondensed) {
        fDebug1 << "[" << ti.first << "] : ";
        c++;
        for(auto &bb : ti.second.BBIds) {
            fDebug1 << bb << " ";
        }
        fDebug1 << "\n";
    }
    fDebug1.close();

    ofstream fDebug0;
    fDebug0.open("ToggleCondensedLineInfo.txt", ios::out | ios::trunc);
    for(auto &ti : ToggleCondensed) {
        fDebug0 << "[" << ti.first << "] : ";
        for(auto &bb : ti.second.BBIds) {
            fDebug0 << bb << " ";
        }
        fDebug0 << ti.second.lineInfo << "\n";
    }
    fDebug0.close();
    
    std::map<int, ToggleData> ToggleRMSubset;
    int rm_count = 0;
    for (const auto& ti : ToggleCondensed) {
        bool is_subset_found = false;

        for (const auto& tj : ToggleCondensed) {
            if (ti.first != tj.first && is_subset(ti.second.BBIds, tj.second.BBIds)) {
                is_subset_found = true;
                break;
            }
        }

        if (!is_subset_found) {
            ToggleRMSubset[rm_count++] = ti.second;
        }
    }

    ofstream fDebug2;
    fDebug2.open("TogglePostDominated.txt", ios::out | ios::trunc);
    for(auto &ti : ToggleRMSubset) {
        fDebug2 << "[" << ti.first << "] : ";
        for(auto &bb : ti.second.BBIds) {
            fDebug2 << bb << " ";
        }
        fDebug2 << "\n";
    }
    fDebug2.close();

    ofstream fDebug3;
    fDebug3.open("TogglePostDominatedLineInfo.txt", ios::out | ios::trunc);
    for(auto &ti : ToggleRMSubset) {
        fDebug3 << "[" << ti.first << "] : ";
        for(auto &bb : ti.second.BBIds) {
            fDebug3 << bb << " ";
        }
        fDebug3 << ti.second.lineInfo << "\n";
    }
    fDebug3.close();


    // mergeTargets(ToggleCondensed, 0.5, 30);
    // ofstream fDebug4;
    // fDebug4.open("ToggleInfoNew.txt", ios::out | ios::trunc);
    // for(auto &ti : ToggleCondensed) {
    //     fDebug4 << "[" << ti.first << "] : ";
    //     for(auto &bb : ti.second.BBIds) {
    //         fDebug4 << bb << " ";
    //     }
    //     fDebug4 << "\n";
    // }
    // fDebug4.close();


    // ofstream fDebug5;
    // fDebug5.open("ToggleInfoNewLineInfo.txt", ios::out | ios::trunc);
    // for(auto &ti : ToggleCondensed) {
    //     fDebug5 << "[" << ti.first << "] : ";
    //     for(auto &bb : ti.second.BBIds) {
    //         fDebug5 << bb << " ";
    //     }
    //     fDebug5 << ti.second.lineInfo << "\n";
    // }
    // fDebug5.close();



    std::cout <<"\n ****** Finished Backward Reachability Analysis (Async Safe) ******\n";

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;

    std::cout <<"\n ****** Total Static Analysis Execution Time: " << duration.count() << "******\n";
    myfile.close();
    ToggleInfo.close();


    std::cout <<"\n ****** [Optimized Toggle Points (Async Safe)] : " << b << " ******\n";
    // allbbcount - average_reachable ; -> pruning bb /(pruning bb / allbbcount) * 100
    int average_reachable = 0;
    if (b > 0) {
        average_reachable = total_reachable / b;
    }
    int pruned_bb = allbbcount - average_reachable;
    double pruning_rate = 0.0;
    if (allbbcount > 0) {
        pruning_rate = ((double)pruned_bb / (double)allbbcount) * 100;
    }

    std::cout<<"\n ****** [All Basic Blocks] : " << allbbcount << " ******\n";

    std::cout<<"\n ****** [Average Reachable Blocks] : " << average_reachable << " ******\n";

    std::cout <<"\n ****** [Pruning Rate] : " << pruning_rate << "% ******\n";

    std::cout << "\n ****** [Final Toggle Points (Async Safe)] : " << c << " ******\n";


    writeModuleToFile(module, "S2FuzzAsyncSafeAnalysis.ll");


}

void S2FuzzManualTargetCollection(SVFModule* svfModule, Module* module, const SVFG* svfg, ICFG* icfg, PTACallGraph* callgraph, const std::string &funcName, const std::string &bbName)
{
    ofstream myfile;
    myfile.open("TargetInfo.txt", ios::out | ios::app);
    // static std::map<std::pair<std::string, unsigned>, const ICFGNode*> fileLineToNode;

    // for (auto &func : *module) {
    //     if (func.isDeclaration()) continue;
    //     for (auto &bb : func) {
    //         for (auto &I : bb) {
    //             if (DILocation *Loc = I.getDebugLoc()) {
    //                 StringRef fname = Loc->getFilename();
    //                 unsigned lnum = Loc->getLine();
    //                 if (fname.str().find(filepath) != std::string::npos && lnum == line) {

    //                     const ICFGNode* iNode = icfg->getBlockICFGNode(&I);
    //                     if(iNode) {
    //                         fileLineToNode[{fname.str(), lnum}] = iNode;
    //                     }
    //                 }
    //             }
    //         }
    //     }
    // }

    // for (auto &entry : fileLineToNode) {
    //     auto &key = entry.first;
    //     auto node = entry.second;
    //     const std::string &fpath = key.first;
    //     unsigned ln = key.second;
        
    //     myfile << fpath << ":" << ln 
    //               << " => " << node->getId() << "\n";
    // }
    std::string fname = "";
    const ICFGNode* lastBBLastInstNode = nullptr;
    for (auto &F : *module) {
        if (F.getName().str().find(funcName) != std::string::npos) {
            fname = F.getName().str();
            if(!F.empty()) {
                for (auto &BB : F) {
                    if (BB.getName().str().find(bbName) != std::string::npos) {
                        if (!BB.empty()) {
                            Instruction &lastInst = BB.back();
                            lastBBLastInstNode = icfg->getBlockICFGNode(&lastInst);
                            break;
                        }
                    }
                }
                // BasicBlock &lastBB = F.back();
                // if(!lastBB.empty()) {
                //     Instruction &firstInst = lastBB.front();
                //     lastBBFirstInstNode = icfg->getBlockICFGNode(&firstInst);
                // }
            }
            
        }
    }
    if (lastBBLastInstNode) {
        myfile << "[Func=" << fname << "] [BBName = " << bbName << "] => ICFGNode "
               << lastBBLastInstNode->getId() << "\n";
    } else {
        myfile << "[Func=" << funcName << "] [BBName = " << bbName << "] No lastBBFirstInstNode found.\n";
    }

    myfile.close();
}



void S2FuzzManualTargetAnalysis(SVFModule* svfModule, Module* module, const SVFG* svfg, ICFG* icfg, PTACallGraph* callgraph, int allbbcount, std::set<const BasicBlock*>& allBasicBlocks, const std::vector<int>& targetNodes)
{
    // ofstream myfile;
    // myfile.open("S2FuzzManualTargetAnalysis.txt", ios::out | ios::app);

    ofstream ToggleInfo;
    ToggleInfo.open("ToggleInfo.txt", ios::out | ios::app);


    ofstream ToggleLineInfo;
    ToggleLineInfo.open("ToggleLineInfo.txt", ios::out | ios::trunc);

    std::set<const ICFGNode*> OptimizedTogglePoints;

    auto start = std::chrono::high_resolution_clock::now();


    for (int nodeId : targetNodes) {
        const ICFGNode* target = icfg->getICFGNode(nodeId);
        OptimizedTogglePoints.insert(target);
    }

    std::set<const llvm::Function *> reachableFunctions;
    const Function* mainFuzzEntry;
    mainFuzzEntry = get_function(svfModule);
    const BasicBlock& mainBB = mainFuzzEntry->front();
    const Instruction *mainInst = &mainBB.front();
    const ICFGNode* mainNode = icfg->getBlockICFGNode(mainInst);

    const SVFFunction* svfMain = svfModule->getSVFFunction(mainFuzzEntry);
    PTACallGraphNode* startNode = callgraph->getCallGraphNode(svfMain);
    reachableFunctions = callgraphTraverse(startNode);
    

    int b = 0;

    std::set<const BasicBlock*> withinSameBB;
    // std::set<const Function*> withinSameFunc;
    int total_reachable = 0;
    int total_reachable_std = 0;
    int total_reachable_source = 0;

    std::map<int, ToggleData> ToggleBasic;

    std::map<const Function*, std::vector<const BasicBlock*>> funcToBBs;
    
    std::cout <<"\n ****** Start Backward Reachability Analysis ******\n";
    for(auto ti = OptimizedTogglePoints.begin(); ti != OptimizedTogglePoints.end(); ti++)
    {
        const ICFGNode* target = *ti;
        // std::cout << "Target : " << target->toString() << "\n";

        /* Conduct Backward Reachability to each toggle Target */
        std::set<const Function*> RustStdLibFunc;
        std::set<const ICFGNode*> checked_iNodes;
        int reachable = 0;
        int reachable_source = 0;
        int reachable_std = 0;
        std::set<const BasicBlock*> ToggleBB;
        std::set<const BasicBlock*> ToggleSTDBB;
        // std::cout << "Target : " << target->toString() << "\n";


        const SVFFunction* svfFun = target->getFun();
        if(svfFun) {

            const Function* func = target->getFun()->getLLVMFun();
            if(isDroplisted(func)) continue;

            for (auto &BB : *func) {
                if(allBasicBlocks.find(&BB)!= allBasicBlocks.end()) { 
                    if(ToggleBB.find(&BB) == ToggleBB.end()) {
                        reachable++;
                        ToggleBB.insert(&BB);
                    }
                }
            }
            
        }
        // /* If the toggle point is in the same basic block, skip analyzing */
        const BasicBlock* targetBB = target->getBB();
        if(targetBB) {
            if(withinSameBB.find(targetBB) != withinSameBB.end()) continue;
            withinSameBB.insert(targetBB);
        }

        if(targetBB) {
            const Function* func = target->getFun()->getLLVMFun();
            if(!func) continue;
            funcToBBs[func].push_back(targetBB);
            // std::cout << "Target Function : " << func->getName().str() << "\n";
            // std::cout << "Target BB : " << targetBB->getName().str() << "\n";
        }

        checked_iNodes = S2FuzzBackwardAnalysis(target, callgraph, mainFuzzEntry, RustStdLibFunc);
        //recheck
        for (auto fi = RustStdLibFunc.begin(); fi != RustStdLibFunc.end(); fi++) {
            const Function* func = *fi;
            const SVFFunction* svfFunc = svfModule->getSVFFunction(func);
            if(svfFunc) {
                // stdfuncs << "Meet Returned Std Function : "<< func->getName().str() << "\n\n";
                PTACallGraphNode* callgraphNode = callgraph->getCallGraphNode(svfFunc);
                if(callgraphNode) {
                    std::set<Function*> functions = traverseSTDfuncs(callgraphNode);
                    int i = 0;
                    for (auto& fun : functions) {
                        if(isBlacklisted(fun) || isDroplisted(fun)) continue;
                        i++;
                        // stdfuncs << "[" << i << "] : Inside Std Function : " << fun->getName().str() << "\n";
                        for (auto &BB : *fun) {
                            if(allBasicBlocks.find(&BB)!= allBasicBlocks.end()) {
                                if(ToggleBB.find(&BB) == ToggleBB.end()) {
                                    reachable++;
                                    ToggleBB.insert(&BB);
                                }
                            }
                        }
                    }
                    // stdfuncs << "\n\n";
                }
            }
        }



        for(auto fi = RustStdLibFunc.begin(); fi!= RustStdLibFunc.end(); fi++) {
            const Function* func = *fi;
            for (auto &BB : *func) {
                if(allBasicBlocks.find(&BB)!= allBasicBlocks.end()) { 
                    if(ToggleSTDBB.find(&BB) == ToggleSTDBB.end()) {
                        reachable_std++;
                        reachable++;
                        ToggleSTDBB.insert(&BB);
                    }
                    
                }   
            }
        }
        


        /* If we cannot find any reachable path from target points, we skip further analysis */
        if(checked_iNodes.empty()) continue;
            


        std::string lineInfoStr;
        /* Get Line Info */
        if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(target))
        {
            Instruction* I = const_cast<Instruction*>(intra->getInst());
            if (DILocation *Loc = I->getDebugLoc()) { 
                unsigned Line = Loc->getLine();
                StringRef File = Loc->getFilename();
                StringRef Directory = Loc->getDirectory();
                
                lineInfoStr = " (File: " + Directory.str() + "/" + File.str() + ", Line: " + std::to_string(Line) + ")";
            }  
            else {
                lineInfoStr = " (" + std::string("File: ") + "Unknown" + ", Line: " + "Unknown)";
            }  
        }
        else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(target))
        {
            Instruction* I = const_cast<Instruction*>(callNode->getCallSite());
            if (DILocation *Loc = I->getDebugLoc()) { 
                unsigned Line = Loc->getLine();
                StringRef File = Loc->getFilename();
                StringRef Directory = Loc->getDirectory();
            
                lineInfoStr = " (File: " + Directory.str() + "/" + File.str() + ", Line: " + std::to_string(Line) + ")";
            } 
            else {
                lineInfoStr = " (" + std::string("File: ") + "Unknown" + ", Line: " + "Unknown)";
            }
        }
        else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(target))
        {
            Instruction* I = const_cast<Instruction*>(RetNode->getCallSite());
            if (DILocation *Loc = I->getDebugLoc()) { 
                unsigned Line = Loc->getLine();
                StringRef File = Loc->getFilename();
                StringRef Directory = Loc->getDirectory();
                
                lineInfoStr = std::string("File: ") + Directory.str() + "/" + File.str() + ", Line: " + std::to_string(Line);
            } 
            else {
                lineInfoStr = " (" + std::string("File: ") + "Unknown" + ", Line: " + "Unknown)";
            }
        }
        else {
            lineInfoStr = " (" + std::string("File: ") + "Unknown" + ", Line: " + "Unknown)";
        }
        ToggleBasic[b].lineInfo = lineInfoStr;
        ToggleBasic[b].targetID = target->getId();
        // if(ToggleBasic[b].lineInfo == " (" + std::string("File: ") + "Unknown" + ", Line: " + "Unknown)") continue;
        /* Get Line Info */


        ToggleInfo << "[" << b <<"] : ";
        

        for(auto ci = checked_iNodes.begin(); ci != checked_iNodes.end(); ci++){
            const BasicBlock* bb = (*ci)->getBB();            

            /* Write Reachable Basic Block Unique ID */
            if(bb) {
                // if(allBasicBlocks.find(bb)!= allBasicBlocks.end()) {
                    if(ToggleBB.find(bb) == ToggleBB.end()) {
                        reachable++;
                        ToggleBB.insert(bb);
                    }

                // }
            }

        }
        // myfile << "[Toggle Point " << b <<"] : " << target->toString() << "\n";
        // myfile << "Reachable BB : " << reachable << "\n\n";

        for(auto &bb : ToggleBB) {
            reachable_source++;
            for (auto &I : *bb) {
                if (auto *CI = dyn_cast<CallInst>(&I)) {
                    if(MDNode *MD = CI->getMetadata("bb_index")) {
                        if (llvm::Metadata* md = MD->getOperand(0)) {
                            if (auto* mdv = llvm::dyn_cast<llvm::ConstantAsMetadata>(md)) {
                                if (llvm::ConstantInt* ci = llvm::dyn_cast<llvm::ConstantInt>(mdv->getValue())) {
                                    uint64_t bbIndexValue = ci->getZExtValue();
                                    ToggleInfo << bbIndexValue << " ";
                                    ToggleBasic[b].BBIds.insert(bbIndexValue);
                                }

                            }
                        }
                    }
                }
            }
        }

        // myfile << "\n\n";
        // myfile << "Toggle Point : " << target->toString() << "\n";
        // myfile << "Reachable BB : " << reachable << "\n\n";
        // for(auto ti = ToggleBB.begin(); ti != ToggleBB.end(); ti++) {
        //     const BasicBlock* bb = *ti;
        //     if(bb) {
        //         myfile << "Func : " << bb->getParent()->getName().str() << " BB : " << bb->getName().str() << "\n";
        //     }
            
        // }

        // for(auto ti = ToggleSTDBB.begin(); ti != ToggleSTDBB.end(); ti++) {
        //     const BasicBlock* bb = *ti;
        //     if(bb) {
        //         myfile << "Func : " << bb->getParent()->getName().str() << " BB : " << bb->getName().str() << "\n";
        //     }
            
        // }    

        ToggleInfo << "\n";   
        total_reachable+= reachable; 
        total_reachable_std += reachable_std;
        total_reachable_source += reachable_source;

        std::cout << "Toggle Point [" << b << "] - Reachable Total BB : " << reachable << "\n";
        std::cout << "Toggle Point [" << b << "] - Reachable STD BB : " << reachable_std << "\n";
        std::cout << "Toggle Point [" << b << "] - Reachable Source BB : " << reachable_source << "\n";
        b++;

    }

    std::set<const ICFGNode*> newOptimizedTogglePoints = OptimizedTogglePoints;

    int target_c = 0;
    std::map<std::pair<const BasicBlock*, const BasicBlock*>, bool> postDomRelation;
    for (auto &entry : funcToBBs) {
        target_c++;
        const Function* F = entry.first;
        const auto &bbList = entry.second;

        if (F->empty()) continue;

        llvm::PostDominatorTree PDT;
        PDT.recalculate(const_cast<Function&>(*F));

        for (size_t i = 0; i < bbList.size(); i++) {
            for(size_t j = 0; j < bbList.size(); j++) {
                if (i == j) continue;
                const BasicBlock* bb1 = bbList[i];
                const BasicBlock* bb2 = bbList[j];
                // std::cout << bb1->getName().str() << " post-dominates? " << bb2->getName().str() << "\n";

                bool isPD = PDT.dominates(bb1, bb2);
                // if(isPD) {
                //     std::cout << "Post-Dominates : " << bb1->getName().str() << " -> " << bb2->getName().str() << "\n";
                // }
                postDomRelation[{bb1, bb2}] = isPD;
            }
        }
    }

    for (auto n1 : OptimizedTogglePoints) {
        const BasicBlock* bb1 = n1->getBB();
        if(!bb1) continue;
        for (auto n2 : OptimizedTogglePoints) {
            if (n1 == n2) continue;
            const BasicBlock* bb2 = n2->getBB();
            if(!bb2) continue;
            
            if(bb1->getParent() != bb2->getParent()) continue;
            
            if (postDomRelation[{bb1, bb2}] == true) {
                newOptimizedTogglePoints.erase(n2);
            }
        }
    }

    int count_toggle_points = 0;
    for (auto n : OptimizedTogglePoints) {
        count_toggle_points++;
    }
    std::cout << " [Non Post-Dominated Toggle Points] : " << count_toggle_points << "\n";

    int post_dominated_toggle_points = 0;
    for (auto n : newOptimizedTogglePoints) {
        post_dominated_toggle_points++;
    }
    std::cout << " [Post-Dominated Toggle Points] : " << post_dominated_toggle_points << "\n";


    std::map<int, ToggleData> ToggleCondensed;

    std::set<std::set<uint64_t>> unique_values;
    int condensed_count = 0;

    int basic_count = 0;
    for (const auto& pair : ToggleBasic) {
        const int& key = pair.first;
        const ToggleData& value = pair.second;
        ToggleLineInfo << "[" << key << "] : ";
        basic_count++;
        for(auto &bb : value.BBIds) {
            ToggleLineInfo << bb << " ";
        }
        ToggleLineInfo << value.lineInfo << " targetID : [" << value.targetID << "]" << "\n";

        if (unique_values.find(value.BBIds) == unique_values.end()) {
            ToggleCondensed[condensed_count++] = value;
            unique_values.insert(value.BBIds); 
        }
    }
    ToggleLineInfo.close();
    
    int c = 0;
    ofstream fDebug1;
    fDebug1.open("ToggleCondensed.txt", ios::out | ios::trunc);
    for(auto &ti : ToggleCondensed) {
        fDebug1 << "[" << ti.first << "] : ";
        c++;
        for(auto &bb : ti.second.BBIds) {
            fDebug1 << bb << " ";
        }
        fDebug1 << "\n";
    }
    fDebug1.close();

    ofstream fDebug0;
    fDebug0.open("ToggleCondensedLineInfo.txt", ios::out | ios::trunc);
    for(auto &ti : ToggleCondensed) {
        fDebug0 << "[" << ti.first << "] : ";
        for(auto &bb : ti.second.BBIds) {
            fDebug0 << bb << " ";
        }
        fDebug0 << ti.second.lineInfo << " targetID : [" << ti.second.targetID << "]" << "\n";
    }
    fDebug0.close();


    std::map<int, ToggleData> TogglePostDominated;
    int pd_count = 0;
    for (const auto& ti : ToggleCondensed) {
        bool is_subset_found = false;

        for (const auto& tj : ToggleCondensed) {
            if (ti.first != tj.first && is_subset(ti.second.BBIds, tj.second.BBIds)) {
                is_subset_found = true;
                break;
            }
        }

        if (!is_subset_found) {
            TogglePostDominated[pd_count++] = ti.second;
        }
    }

    ofstream fDebug2;
    fDebug2.open("TogglePostDominated.txt", ios::out | ios::trunc);
    for(auto &ti : TogglePostDominated) {
        fDebug2 << "[" << ti.first << "] : ";
        for(auto &bb : ti.second.BBIds) {
            fDebug2 << bb << " ";
        }
        fDebug2 << "\n";
    }
    fDebug2.close();

    ofstream fDebug3;
    fDebug3.open("TogglePostDominatedLineInfo.txt", ios::out | ios::trunc);
    for(auto &ti : TogglePostDominated) {
        fDebug3 << "[" << ti.first << "] : ";
        for(auto &bb : ti.second.BBIds) {
            fDebug3 << bb << " ";
        }
        fDebug3 << ti.second.lineInfo << " targetID : [" << ti.second.targetID << "]" << "\n";
    }
    fDebug3.close();

    std::cout << "Provided Target ICFG Node IDs : ";
    int t = 0;
    for (int nodeId : targetNodes) {
        std::cout << t << " : [" << nodeId << "] ";
        t++;
    }
    std::cout << "\n";


    int optimized_t = 0;
    std::cout << "Optimized Target ICFG Node IDs : ";
    for(auto &ti : TogglePostDominated) {
        std::cout << optimized_t << " : [" << ti.second.targetID << "] ";
        optimized_t++;
    }
    std::cout << "\n";


    // for (auto ti = OptimizedTogglePoints.begin(); ti != OptimizedTogglePoints.end(); ti++) {
    //     const ICFGNode* target = *ti;
    // }



    std::cout <<"\n ****** Finished Backward Reachability Analysis ******\n";

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;

    std::cout <<"\n ****** Total Static Analysis Execution Time: " << duration.count() << "******\n";
    ToggleInfo.close();


    std::cout <<"\n ****** [Final Toggle Points] : " << b << " ******\n";


    int average_total_reachable = 0;
    int average_reachable_std = 0;
    int average_reachable_source = 0;
    if (b > 0) {
        average_total_reachable = total_reachable / b;
        average_reachable_std = total_reachable_std / b;
        average_reachable_source = total_reachable_source / b;
    }
    int total_pruned_bb = allbbcount - average_total_reachable;
    double total_pruning_rate = 0.0;
    if (allbbcount > 0) {
        total_pruning_rate = ((double)total_pruned_bb / (double)allbbcount) * 100;
    }


    std::cout<<"\n ****** [All Basic Blocks] : " << allbbcount << " ******\n";

    std::cout<<"\n ****** [Average Total Reachable Blocks] : " << average_total_reachable << " ******\n";

    std::cout<<"\n ****** [Average Reachable STD BB] : " << average_reachable_std << " ******\n";

    std::cout<<"\n ****** [Average Reachable Source BB] : " << average_reachable_source << " ******\n";

    std::cout <<"\n ****** [Total Pruning Rate] : " << total_pruning_rate << "% ******\n";

    // std::cout << "Compute Pruning Rate for without Standard Library BBs by using [Total Source BB - Average Reachable Source BB] / [Total Source BB]"  << "\n";

    // std::cout << "\n ****** [Final Toggle Points] : " << c << " ******\n"

    writeModuleToFile(module, "S2FuzzManualTargetAnalysis.ll");

}



void ParallelS2FuzzStaticAnalysis(SVFModule* svfModule, Module* module, const SVFG* svfg, ICFG* icfg, PTACallGraph* callgraph, size_t fuzzing_options, bool debug_pruning_rate, int allbbcount, std::set<const BasicBlock*>& allBasicBlocks, int thread_count)
{
    // StaticAnalysis Debugging
    ofstream myfile;
    myfile.open("S2Fuzz.txt", ios::out | ios::trunc);

    ofstream ToggleInfo;
    ToggleInfo.open("ToggleInfo.txt", ios::out | ios::app);

    if(debug_pruning_rate) {
        std::cout << "\n ****** Static Analysis with Pruning Rate Information  ******\n";
    }
    else {
        std::cout << "\n ****** Static Analysis without Pruning Rate Information (for static time evaluation) ******\n";
    }
    
    int NUM_THREADS = thread_count;
    std::cout << "\n ******" << "Total Enable Threads : " << NUM_THREADS << "******\n";

    /* Get all reachable functions from the Fuzz Entry Function */
    std::set<const llvm::Function *> reachableFunctions;
    const Function* mainFuzzEntry;
    /* fuzzing_options == 0 --> afl.rs */
    if(fuzzing_options == 0) {
        mainFuzzEntry = get_function(svfModule);
        const BasicBlock& mainBB = mainFuzzEntry->front();
        const Instruction *mainInst = &mainBB.front();
        const ICFGNode* mainNode = icfg->getBlockICFGNode(mainInst);

        const SVFFunction* svfMain = svfModule->getSVFFunction(mainFuzzEntry);
        PTACallGraphNode* startNode = callgraph->getCallGraphNode(svfMain);
        reachableFunctions = callgraphTraverse(startNode);
        // reachableFunctions = TraverseCFGReachableFunc(mainNode);
    }

    // @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@Reachable user defined functions reachable from main, not standard library@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
    std::set<const llvm::Function *> userDefinedFunctions;
    std::mutex userDefFuncMutex;
    std::vector<std::set<const llvm::Function *>> userDefinedFuncs(NUM_THREADS);
    std::vector<std::thread> userDefFuncThreads;
    int groupIdx_userDefFunc = 0;
    int GroupSize_userDefFunc = reachableFunctions.size() / NUM_THREADS;

    for (auto fi = reachableFunctions.begin(); fi != reachableFunctions.end(); fi++) {
        userDefinedFuncs[groupIdx_userDefFunc].insert(*fi);
        if (userDefinedFuncs[groupIdx_userDefFunc].size() >= GroupSize_userDefFunc && (groupIdx_userDefFunc < (NUM_THREADS - 1))) {
            groupIdx_userDefFunc++;
        }
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        if(userDefinedFuncs[i].empty()) continue;
        userDefFuncThreads.emplace_back(ParallelUserDefinedFunctionsIdentification, std::ref(userDefinedFuncs[i]), std::ref(userDefFuncMutex), std::ref(userDefinedFunctions));
    }

    for (auto & thread : userDefFuncThreads) {
        thread.join();
    }

    // for (auto fi = userDefinedFunctions.begin(); fi != userDefinedFunctions.end(); fi++) {
    //     std::cout << "User Defined Function : " << (*fi)->getName().str() << "\n";
    // }
    //@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

    // @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@Reachable outermost functions@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
    std::set<const llvm::Function*> outermostFunctions;
    std::mutex outermostFuncMutex;
    std::set<std::string> outermostFunctionsName;
    std::mutex outermostFuncNameMutex;

    std::vector<std::set<const llvm::Function *>> userDefinedFuncsForOutermost(NUM_THREADS);
    std::vector<std::thread> outermostFuncThreads;
    int groupIdx_outermostFunc = 0;
    int GroupSize_outermostFunc = userDefinedFunctions.size() / NUM_THREADS;
    // if (GroupSize_outermostFunc == 0) GroupSize_outermostFunc = 1; 

    for (auto fi = userDefinedFunctions.begin(); fi != userDefinedFunctions.end(); fi++) {
        userDefinedFuncsForOutermost[groupIdx_outermostFunc].insert(*fi);
        if (userDefinedFuncsForOutermost[groupIdx_outermostFunc].size() >= GroupSize_outermostFunc && (groupIdx_outermostFunc < (NUM_THREADS - 1))) {
            groupIdx_outermostFunc++;
        }
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        if (userDefinedFuncsForOutermost[i].empty()) continue;
        outermostFuncThreads.emplace_back(
            ParallelOutermostFunctionsIdentification,
            std::ref(userDefinedFuncsForOutermost[i]),
            std::ref(reachableFunctions),
            std::ref(userDefinedFunctions),
            std::ref(outermostFuncMutex),
            std::ref(outermostFunctions),
            std::ref(outermostFuncNameMutex),
            std::ref(outermostFunctionsName)
        );
    }

    for (auto & thread : outermostFuncThreads) {
        thread.join();
    }

    // for (auto fi = outermostFunctions.begin(); fi != outermostFunctions.end(); fi++) {
    //     std::cout << "Outermost Function : " << (*fi)->getName().str() << "\n";
    // }



    //@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
 
    /* Optmized Toggle Points ICFNode */
    std::set<const ICFGNode*> OptimizedTogglePoints;
    std::set<std::string> targetCallSites;
    

    /* Raw Pointer Load/Store -> OptimizedTogglePoints && Allocation Sites stemming from Raw Pointers -> topObjs (from only reachable functions) */
    std::set<const SVFGNode*> topObjs = S2FuzzRawPtrInstructions(module, svfg, callgraph, OptimizedTogglePoints, reachableFunctions, outermostFunctions, targetCallSites, userDefinedFunctions);
    
    /* Raw Pointer Related Heap Objects */
    std::set<const SVFGNode*> heapObjs;
    std::set<const SVFGEdge*> heapvisited;

    /* Raw Pointer Related Stack Objects */
    std::set<const SVFGNode*> stackObjs;
    std::set<const SVFGEdge*> stackvisited;

    /* Toggle Points ICFNode */
    std::set<const ICFGNode*> toggleNode;
    std::set<const ICFGNode*> toggleHeapNode;
    std::set<const ICFGNode*> toggleStackNode;

    
    // /* Count All Basic Blocks from Fuzz Entry Point */
    // int allbbcount = 0;
    // std::set<const BasicBlock*> allBasicBlocks = countAllBB(svfModule, icfg, &allbbcount, fuzzing_options);

    


    /* Measure Static Analysis Time*/
    auto start = std::chrono::high_resolution_clock::now();

    
    
    std::set<const SVFGNode*> heapNodes;
    std::set<const SVFGNode*> stackNodes;

    /* HeapOrStackidentification Parallel Optimization */
    std::mutex mtx_heap_or_stack_identification;
    std::vector<std::thread> memTypeIdentThreads;


    std::vector<std::set<const SVFGNode*>> subTopObjs(NUM_THREADS);   
    int groupIdx = 0;
    // size_t a = 0;
    int GroupSize = topObjs.size() / NUM_THREADS;
    int a = 0;
    // Initialize subTopObjs
    for (auto ii = topObjs.begin(); ii != topObjs.end(); ii++) {
        a++;
        subTopObjs[groupIdx].insert(*ii);
        if (subTopObjs[groupIdx].size() >= GroupSize && (groupIdx < (NUM_THREADS - 1))) {
            groupIdx++;
        }
    }    
    std::cout << "\n ******" << "Total Allocation Sites : " << a << "******\n";

    // thread group for each group
    for (int i = 0; i < NUM_THREADS; i++) {
        if(subTopObjs[i].size() == 0) continue;
        memTypeIdentThreads.emplace_back(ParallelHeapOrStackIdentificationWrapper, std::ref(subTopObjs[i]), std::ref(heapObjs), std::ref(stackObjs), std::ref(heapNodes), std::ref(stackNodes), std::ref(mtx_heap_or_stack_identification));
    }

    // for(auto ii = topObjs.begin(); ii != topObjs.end(); ii++) 
    // {
    //     const SVFGNode* topPtr = *ii;
    //     memTypeIdentThreads.emplace_back(ParallelHeapOrStackIdentification, topPtr, std::ref(heapObjs), std::ref(stackObjs), std::ref(heapNodes), std::ref(stackNodes), std::ref(mtx_heap_or_stack_identification));
    // }
    

    for (auto& thread : memTypeIdentThreads) {
        thread.join();
    }

    int l = 0;
    for(auto & ho : heapObjs) {
        l++;
    }
    std::cout << "\n ******" << "Total Heap Allocation Sites : " << l << "******\n";
    l = 0;
    for(auto & so : stackObjs) {
        l++;
    }
    std::cout << "\n ******" << "Total Stack Allocation Sites : " << l << "******\n";


    std::cout << "\n ******Finished Identifying HeapOrStack Allocation Sites******\n";
    
    /* HeapOrStackidentification Parallel Optimization */

    // std::vector<std::thread> heapAccessThreads;
    // std::vector<std::vector<const SVFGNode*>> subHeapObjs(NUM_THREADS);
    // std::set<const SVFGNode*> NodesForAfterDrop;


    // std::mutex visitedMutex;
    // std::mutex togglePointsMutex;
    // std::mutex NodesForAfterDropMutex;

    // int groupIdx_heapAccess = 0;
    // int GroupSize_heapAccess = heapObjs.size() / NUM_THREADS;
    // for (auto hi = heapObjs.begin(); hi != heapObjs.end(); hi++) {
    //     subHeapObjs[groupIdx_heapAccess].push_back(*hi);
    //     if (subHeapObjs[groupIdx_heapAccess].size() >= GroupSize_heapAccess && (groupIdx_heapAccess < (NUM_THREADS - 1))) {
    //         groupIdx_heapAccess++;
    //     }
    // }

    // for (int i = 0; i < NUM_THREADS; i++) {
    //     if(subHeapObjs[i].empty()) continue;
    //     heapAccessThreads.emplace_back([&, i]() {
    //         for (auto sNode : subHeapObjs[i]) {
    //             ParallelS2FuzzHeapAccessSitesWrapper(sNode, std::ref(toggleHeapNode), std::ref(togglePointsMutex), std::ref(outermostFunctionsName), std::ref(NodesForAfterDrop), std::ref(NodesForAfterDropMutex));
    //         }
    //     });
    // }

    // for (auto & thread : heapAccessThreads) {
    //     thread.join();
    // }

    // for(auto &node : NodesForAfterDrop) {
    //     std::cout << "Node for After Drop : " << node->toString() << "\n";
    // }

    // std::set<std::string> doubleDropFunctions;
    // std::set<std::string> outermostDropFunctions;
    // std::mutex doubleDropFunctionsMutex;
    // std::mutex outermostDropFunctionsMutex;


    // ParallelS2FuzzHeapAccessSitesWrapper

    std::set<std::string> DoubleDropFuncs;
    
    for(auto hi = heapObjs.begin(); hi != heapObjs.end(); hi++) {
        const SVFGNode* heapObj = *hi;
        MarkHeapAllocationSites(heapObj);
        S2FuzzHeapAccessSites(heapObj, toggleHeapNode, heapvisited, reachableFunctions, outermostFunctionsName, &DoubleDropFuncs);
    }
    
    /* Marking Stack/Heap Access Sites Parallel Optimization */
    std::mutex mtx_marking;
    std::thread stackThread(processStackTargets, std::cref(stackObjs), std::ref(toggleStackNode), std::ref(stackvisited), std::ref(mtx_marking));
    stackThread.join();
    // std::thread heapThread(processHeapTargets, std::cref(heapObjs), std::ref(toggleHeapNode), std::ref(heapvisited), std::ref(mtx_marking), std::cref(reachableFunctions));
    // heapThread.join();


    int m = 0;
    std::set<const ICFGNode*> toggleNodes;
    for(auto &toggleheap : toggleHeapNode) {
        m++;
    }
    std::cout << "\n ******Total heap toggle Nodes : " << m << "******\n";
    m = 0;
    for(auto &togglestack : toggleStackNode) {
        m++;
    }
    std::cout << "\n ******Total stack toggle Nodes : " << m << "******\n";


    for(auto ti = toggleHeapNode.begin(); ti != toggleHeapNode.end(); ti++)
    {
        const ICFGNode* iNode = *ti;
        const SVFFunction* svfFun = iNode->getFun();
        if(svfFun) {
            const Function* func = iNode->getFun()->getLLVMFun();
            // If the function is not in the reachable functions, then it is not a toggle point
            if(reachableFunctions.find(func) == reachableFunctions.end()) continue;
            
            // // If the function is in the standard library, then it should be performed with outermost functions
            if(userDefinedFunctions.find(func) == userDefinedFunctions.end())
            {
                if (targetCallSites.find(func->getName().str()) == targetCallSites.end()) {
                    targetCallSites.insert(func->getName().str());
                }
            }

            // if(outermostFunctions.find(func) != outermostFunctions.end() && targetCallSites.find(func->getName().str()) == targetCallSites.end()) {
            //     targetCallSites.insert(func->getName().str());
            // }

            // Else if the fnction is in the user defined functions, then it is a toggle point directly
            else {
                if(OptimizedTogglePoints.find(iNode) == OptimizedTogglePoints.end()) {
                    OptimizedTogglePoints.insert(iNode);
                }
            }

        }
        
    }

    for(auto ti = toggleStackNode.begin(); ti != toggleStackNode.end(); ti++)
    {
        const ICFGNode* iNode = *ti;
        const SVFFunction* svfFun = iNode->getFun();
        if(svfFun) {
            const Function* func = iNode->getFun()->getLLVMFun();
            // If the function is not in the reachable functions, then it is not a toggle point
            if(reachableFunctions.find(func) == reachableFunctions.end()) continue;
            
            // // If the function is in the standard library, then it should be performed with outermost functions
            if(userDefinedFunctions.find(func) == userDefinedFunctions.end())
            {
                if (targetCallSites.find(func->getName().str()) == targetCallSites.end()) {
                    targetCallSites.insert(func->getName().str());
                }
            }

            else {
                if(OptimizedTogglePoints.find(iNode) == OptimizedTogglePoints.end()) {
                    OptimizedTogglePoints.insert(iNode);
                }
            }

        }
        
    }

    for (auto ti = DoubleDropFuncs.begin(); ti != DoubleDropFuncs.end(); ti++) {
        if (targetCallSites.find(*ti) == targetCallSites.end()) {
            targetCallSites.insert(*ti);
        }
    }


    for (const Function* userFunc : userDefinedFunctions) {
        for (const BasicBlock &BB : *userFunc) {
            for (const Instruction &I : BB) {
                if (const CallInst *callInst = dyn_cast<CallInst>(&I) ) {
                    const Function* calledFunc = callInst->getCalledFunction();
                    if(!calledFunc) continue;
                    std::string callingFunc = calledFunc->getName().str();
                    if(targetCallSites.find(callingFunc) != targetCallSites.end()) {

                        Instruction *inst = const_cast<Instruction*>(&I);
                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "stdCallSite");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("stdCallSite", node);

                        const ICFGNode* iNode = icfg->getBlockICFGNode(&I);
                        if (iNode) {
                            OptimizedTogglePoints.insert(iNode);
                        }
                    }

                }

                else if (const InvokeInst *invokeInst = dyn_cast<InvokeInst>(&I)) {
                    const Function* calledFunc = invokeInst->getCalledFunction();
                    if(!calledFunc) continue;
                    std::string callingFunc = calledFunc->getName().str();
                    if(targetCallSites.find(callingFunc) != targetCallSites.end()) {

                        Instruction *inst = const_cast<Instruction*>(&I);
                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "stdCallSite");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("stdCallSite", node);

                        const ICFGNode* iNode = icfg->getBlockICFGNode(&I);
                        if (iNode) {
                            OptimizedTogglePoints.insert(iNode);
                        }
                    }
                }


            }
        }
    }

    int z = 0;
    for(auto i = OptimizedTogglePoints.begin(); i != OptimizedTogglePoints.end(); i++) {
        z++;
    }
    std::cout << "\n ******Total Optimized Toggle Points : " << z << "******\n";



    std::cout << "\n ******Finished Marking Target Sites******\n";

    

    /* Marking Stack/Heap Access Sites Parallel Optimization */

    /* S2FuzzBackwardAnalysis Parallel Optimization */
    int b = 0;
    std::set<const BasicBlock*> withinSameBB;
    // std::set<const Function*> withinSameFunc;
    int total_reachable = 0;

    std::mutex mtx_backwardAnalysis;
    std::vector<std::thread> backwardThreads;
    std::vector<std::set<const ICFGNode*>> subOptimizedTogglePoints(NUM_THREADS);
    int groupIdx_backward = 0;
    int GroupSize_backward = OptimizedTogglePoints.size() / NUM_THREADS;
    std::map<int, ToggleData> ToggleBasic;
    std::mutex mtx_ToggleBasic;


    for(auto ti = OptimizedTogglePoints.begin(); ti != OptimizedTogglePoints.end(); ti++) {
        subOptimizedTogglePoints[groupIdx_backward].insert(*ti);
        if (subOptimizedTogglePoints[groupIdx_backward].size() >= GroupSize_backward && (groupIdx_backward < (NUM_THREADS - 1))) {
            groupIdx_backward++;
        }
    }

    int global_bIndex = 0;
    std::mutex mtx_global_bIndex;

    int TotalReachableArray[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        TotalReachableArray[i] = 0;
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        if(subOptimizedTogglePoints[i].size() == 0) continue;
        backwardThreads.emplace_back(ParallelBackwardAnalysisWrapper, std::ref(subOptimizedTogglePoints[i]), std::ref(withinSameBB), std::ref(TotalReachableArray[i]), std::ref(global_bIndex), std::ref(mtx_global_bIndex), std::ref(ToggleInfo), std::ref(myfile), std::ref(allBasicBlocks), std::ref(callgraph), std::ref(mainFuzzEntry), std::ref(mtx_backwardAnalysis), std::ref(ToggleBasic), std::ref(mtx_ToggleBasic));
    }

    // for(auto ti = OptimizedTogglePoints.begin(); ti != OptimizedTogglePoints.end(); ti++)
    // {
    //     const ICFGNode* target = *ti;
    //     backwardThreads.emplace_back(ParallelBackwardAnalysis, target, std::ref(withinSameBB), std::ref(total_reachable), std::ref(b), std::ref(ToggleInfo), std::ref(myfile), std::ref(allBasicBlocks),
    //                                  std::ref(callgraph), std::ref(mainFuzzEntry), std::ref(mtx_backwardAnalysis));
    // }

    for (auto & thread : backwardThreads) {
        thread.join();
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        total_reachable+=TotalReachableArray[i];
    }


    

    std::map<int, ToggleData> ToggleCondensed;

    std::set<std::set<uint64_t>> unique_values;
    int condensed_count = 0;

    for (const auto& pair : ToggleBasic) {
        const int& key = pair.first;
        const ToggleData& value = pair.second;

        if (unique_values.find(value.BBIds) == unique_values.end()) {
            ToggleCondensed[condensed_count++] = value;
            unique_values.insert(value.BBIds); 
        }
    }

    
    int c = 0;
    ofstream fDebug1;
    fDebug1.open("ToggleCondensed.txt", ios::out | ios::trunc);
    for(auto &ti : ToggleCondensed) {
        fDebug1 << "[" << ti.first << "] : ";
        c++;
        for(auto &bb : ti.second.BBIds) {
            fDebug1 << bb << " ";
        }
        fDebug1 << "\n";
    }
    fDebug1.close();

    ofstream fDebug0;
    fDebug0.open("ToggleCondensedLineInfo.txt", ios::out | ios::trunc);
    for(auto &ti : ToggleCondensed) {
        fDebug0 << "[" << ti.first << "] : ";
        for(auto &bb : ti.second.BBIds) {
            fDebug0 << bb << " ";
        }
        fDebug0 << ti.second.lineInfo << "\n";
    }
    fDebug0.close();

    

    /* S2FuzzBackwardAnalysis Parallel Optimization */

    
    std::cout <<"\n ****** Finished Backward Reachability Analysis ******\n";

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;

    std::cout <<"\n ****** Total Static Analysis Execution Time: " << duration.count() << "******\n";
    myfile.close();
    ToggleInfo.close();


    // std::cout <<"\n ****** [Optimized Toggle Points] : " << b << " ******\n";
    // // allbbcount - total_reachable/global_bIndex ; -> pruning bb /(pruning bb / allbbcount) * 100
    // int pruned_bb = allbbcount - (total_reachable/b);
    // double pruning_rate = ((double)pruned_bb / (double)allbbcount) * 100;

    // std::cout<<"\n ****** [All Basic Blocks] : " << allbbcount << " ******\n";

    // std::cout<<"\n ****** [Average Reachable Blocks] : " << total_reachable/b << " ******\n";

    // std::cout <<"\n ****** [Pruning Rate] : " << pruning_rate << "% ******\n";

    writeModuleToFile(module, "S2FuzzAnalysis.ll");

}





// void RustGoBaseModeAnalysis(SVFModule* svfModule, Module* module, const SVFG* svfg, ICFG* icfg, PTACallGraph* callgraph, int allbbcount, std::set<const BasicBlock*>& allBasicBlocks)
// {
//     ofstream myfile;
//     myfile.open("Mode_BASE.txt", ios::out | ios::app);
//     std::set<const SVFGNode*> topObjs = SanitizerRawPtrInstructions(module, svfg, callgraph);

//     /* Toggle Points ICFNode */
//     std::set<const ICFGNode*> TogglePoints;

//     /* Raw Pointer Related Heap Objects */
//     std::set<const SVFGNode*> heapObjs;
//     std::set<const SVFGNode*> heapRelatedAllInst;
//     std::set<const SVFGEdge*> heapvisited;

//     /* Raw Pointer Related Stack Objects */
//     std::set<const SVFGNode*> stackObjs;
//     std::set<const SVFGNode*> stackRelatedAllInst;
//     std::set<const SVFGEdge*> stackvisited;

//     /* Toggle Points ICFNode */
//     std::set<const ICFGNode*> toggleNode;

//     /* Counting Stack/Heap Object related to the Raw Pointers */
//     int a = 0;
//     /* Measure Static Analysis Time*/
//     auto start = std::chrono::high_resolution_clock::now();


//     for(auto ii = topObjs.begin(); ii != topObjs.end(); ii++)
//     {
//         a++;
//     }

//     std::cout << "\n******" << "Total Allocation Sites : " << a << "******\n";
    
//     std::set<const SVFGNode*> heapNodes;
//     std::set<const SVFGNode*> stackNodes;

//     for(auto ii = topObjs.begin(); ii != topObjs.end(); ii++)
//     {
//         const SVFGNode* topPtr = *ii;  
//         HeapOrStackIdentification(topPtr, heapObjs, stackObjs, heapNodes, stackNodes);
//     }

//     std::cout << "\n ******Finished Identifying HeapOrStack Allocation Sites******\n";
    
//     std::set<const llvm::Function *> reachableFunctions;
//     reachableFunctions = {};
//     std::set<std::string> outermostFunctionsName;
//     std::set<std::string> doubleDropfuncs;
//     for(auto hi = heapObjs.begin(); hi != heapObjs.end(); hi++)
//     {
//         const SVFGNode* heapObj = *hi;
//         MarkHeapAllocationSites(heapObj);
//         S2FuzzHeapAccessSites(heapObj, toggleNode, heapvisited, reachableFunctions, outermostFunctionsName, &doubleDropfuncs);
//     }

//     std::cout << "\n ******Finished Marking Heap Access Sites******\n";
//     for(auto si = stackObjs.begin(); si != stackObjs.end(); si++)
//     {
//         const SVFGNode* stackObj = *si;
//         MarkStackAllocationSites(stackObj);
//         S2FuzzStackAccessSites(stackObj, toggleNode, stackvisited);
//     }
//     std::cout << "\n ******Finished Marking Stack Access Sites******\n";    


//     a = 0;
//     for(auto ti = toggleNode.begin(); ti != toggleNode.end(); ti++)
//     {
        
//         const ICFGNode* iNode = *ti;
//         const SVFFunction* svfFun = iNode->getFun();
//         if(svfFun) {
//             const Function* func = iNode->getFun()->getLLVMFun();
        
//             if(TogglePoints.find(iNode) == TogglePoints.end()) {
//                 a++;
//                 TogglePoints.insert(iNode);
//             }

//         }
        
//     }

//     std::cout <<"\n ****** [Total Target Points] : " << a << " ******\n";
//     int total_reachable = 0;
//     std::set<std::string> allowlist;
//     std::set<const BasicBlock*> reachableBB;
//     std::set<const Function*> reachableFuncs;

//     std::cout <<"\n ****** Start Rust-Unaware Backward Reachability Analysis ******\n";
//     for(auto tp = TogglePoints.begin(); tp != TogglePoints.end(); tp++)
//     {
//         const ICFGNode* target = *tp;
//         traverseBackwardsFlowSensitiveForRust(icfg, target, callgraph, allowlist);
//     }

//     instrumentUnreachableFuncs(*module, allowlist);

//     for (auto &func : *module) {
//         if (func.isDeclaration()) continue;
//         if (allowlist.find(func.getName().str()) != allowlist.end()) {
//             reachableFuncs.insert(&func);
//             // for (auto &BB : func) {
//             //     total_reachable++;
//             //     reachableBB.insert(&BB);
//             //     myfile << "Reachable BB : " << func.getName().str() << " : " << BB.getName().str() << "\n";
//             // }
//         }
//     }
    
//     for (auto rf = reachableFuncs.begin(); rf != reachableFuncs.end(); rf++) {
//         const Function* func = *rf;
//         for (auto &BB : *func) {
//             if(reachableBB.find(&BB) == reachableBB.end()) {  
//                 total_reachable++;
//                 reachableBB.insert(&BB);
//             }
//         }
//     }

//     auto end = std::chrono::high_resolution_clock::now();
//     std::chrono::duration<double> duration = end - start;

//     std::cout <<"\n ****** Total Static Analysis Execution Time: " << duration.count() << "******\n";
//     myfile.close();

//     std::cout <<"\n ****** [Target Points] : " << a << " ******\n";
//     int pruned_bb = allbbcount - total_reachable;
//     double pruning_rate = std::max(0.0, ((double)pruned_bb / (double)allbbcount) * 100);

//     std::cout<<"\n ****** [All Basic Blocks] : " << allbbcount << " ******\n";

//     std::cout<<"\n ****** [Total Reachable Blocks] : " << total_reachable << " ******\n";

//     std::cout <<"\n ****** [Pruning Rate] : " << pruning_rate << "% ******\n";

//     writeModuleToFile(module, "BaseMode.ll");


// }


// void RustGoMode1Analysis(SVFModule* svfModule, Module* module, const SVFG* svfg, ICFG* icfg, PTACallGraph* callgraph, int allbbcount, std::set<const BasicBlock*>& allBasicBlocks)
// {
//     ofstream myfile;
//     myfile.open("Mode1.txt", ios::out | ios::app);
//     ofstream ToggleInfo;
//     ToggleInfo.open("ToggleInfo.txt", ios::out | ios::app);


//     /* Get all reachable functions from the Fuzz Entry Function */
//     const Function* mainFuzzEntry = get_function(svfModule);
//     const BasicBlock& mainBB = mainFuzzEntry->front();
//     const Instruction *mainInst = &mainBB.front();
//     const ICFGNode* mainNode = icfg->getBlockICFGNode(mainInst);
//     const SVFFunction* svfMain = svfModule->getSVFFunction(mainFuzzEntry);
//     PTACallGraphNode* startNode = callgraph->getCallGraphNode(svfMain);
//     std::set<const llvm::Function *> reachableFunctions = callgraphTraverse(startNode);

//     /* Optmized Toggle Points ICFNode */
//     std::set<const ICFGNode*> OptimizedTogglePoints;
//     std::set<std::string> targetCallSites;

//     /* Raw Pointer Load/Store -> OptimizedTogglePoints && Allocation Sites stemming from Raw Pointers -> topObjs (from only reachable functions) */
//     std::set<const SVFGNode*> topObjs = S2FuzzRawPtrInstructions(module, svfg, callgraph, OptimizedTogglePoints, reachableFunctions, reachableFunctions, targetCallSites, reachableFunctions);
    
//     /* Raw Pointer Related Heap Objects */
//     std::set<const SVFGNode*> heapObjs;
//     std::set<const SVFGEdge*> heapvisited;

//     /* Raw Pointer Related Stack Objects */
//     std::set<const SVFGNode*> stackObjs;
//     std::set<const SVFGEdge*> stackvisited;

//     /* Toggle Points ICFNode */
//     std::set<const ICFGNode*> toggleNode;

//     /* Counting Stack/Heap Object related to the Raw Pointers */
//     int a = 0;



//     /* Measure Static Analysis Time*/
//     auto start = std::chrono::high_resolution_clock::now();

//     for(auto ii = topObjs.begin(); ii != topObjs.end(); ii++)
//     {
//         a++;
//     }

//     std::cout << "\n ******" << "Total Allocation Sites : " << a << "******\n";
    
//     std::set<const SVFGNode*> heapNodes;
//     std::set<const SVFGNode*> stackNodes;
    
    
//    /* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@Before Optimization@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */ 
//     for(auto ii = topObjs.begin(); ii != topObjs.end(); ii++)
//     {
//         const SVFGNode* topPtr = *ii;  
//         HeapOrStackIdentification(topPtr, heapObjs, stackObjs, heapNodes, stackNodes);
//     }
//    /* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@Before Optimization@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */ 

//     std::set<std::string> outermostFunctionsName;
//     std::set<std::string> doubleDropfuncs;
//    /* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@After Optimization@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */ 
//     for(auto hi = heapObjs.begin(); hi != heapObjs.end(); hi++)
//     {
//         const SVFGNode* heapObj = *hi;
//         MarkHeapAllocationSites(heapObj);        
//         S2FuzzHeapAccessSites(heapObj, toggleNode, heapvisited, reachableFunctions, outermostFunctionsName, &doubleDropfuncs);
        
//     }

//     std::cout << "\n ******Finished Marking Heap Access Sites******\n";

//     for(auto si = stackObjs.begin(); si != stackObjs.end(); si++)
//     {
//         const SVFGNode* stackObj = *si;
//         MarkStackAllocationSites(stackObj);
//         S2FuzzStackAccessSites(stackObj, toggleNode, stackvisited);
        
//     }

//     std::cout << "\n ******Finished Marking Stack Access Sites******\n";


//     a = 0;
    


//     for(auto ti = toggleNode.begin(); ti != toggleNode.end(); ti++)
//     {
        
//         const ICFGNode* iNode = *ti;
//         const SVFFunction* svfFun = iNode->getFun();
//         if(svfFun) {
//             const Function* func = iNode->getFun()->getLLVMFun();
//             // If the function is not in the reachable functions, then it is not a toggle point
//             if(reachableFunctions.find(func) == reachableFunctions.end()) continue;
            

//             if(OptimizedTogglePoints.find(iNode) == OptimizedTogglePoints.end()) {
//                 a++;
//                 OptimizedTogglePoints.insert(iNode);
//             }

//         }
        
//     }


//     int b = 0;
//     std::set<const BasicBlock*> withinSameBB;
//     std::set<const BasicBlock*> reachableBB;
//     std::set<std::string> total_allowlist;
//     int total_reachable = 0;

//     std::cout <<"\n ****** Start Rust-Unaware Backward Reachability Analysis ******\n";
//     for(auto ti = OptimizedTogglePoints.begin(); ti != OptimizedTogglePoints.end(); ti++)
//     {
//         /* If toggle points are in standard libraries, skip analyzing */ 
//         const ICFGNode* target = *ti;
//         std::set<std::string> allowlist;
//         std::set<const Function*> reachableFuncs;
//         /* Conduct Backward Reachability to each toggle Target */
//         std::set<const ICFGNode*> checked_iNodes;
//         std::set<const BasicBlock*> ToggleBB;
//         int reachable = 0;

//         const SVFFunction* svfFun = target->getFun();
//         if(svfFun) {
//             const Function* func = target->getFun()->getLLVMFun();
//             if(isDroplisted(func)) continue;
//             if(isBlacklisted(func) && !isCMLibrarylisted(func)) continue;
//         }

//         // /* If the toggle point is in the same basic block, skip analyzing */
//         const BasicBlock* targetBB = target->getBB();
//         if(targetBB) {
//             if(withinSameBB.find(targetBB) != withinSameBB.end()) continue;
//             withinSameBB.insert(targetBB);
//         }

//         traverseBackwardsFlowSensitiveMode1(icfg, target, callgraph, allowlist, reachableFuncs);

//         for(auto al = allowlist.begin(); al != allowlist.end(); al++) {
//             total_allowlist.insert(*al);
//         }

//         ToggleInfo << "[" << b <<"] : ";
//         for( auto f = reachableFuncs.begin(); f != reachableFuncs.end(); f++)
//         {
//             const Function* allowFunc = *f;
//             if(!allowFunc->isDeclaration())
//             {
//                 for (auto &bb : *allowFunc) {
//                     if(reachableBB.find(&bb) == reachableBB.end()) {
//                         reachable++;
//                         reachableBB.insert(&bb);
//                     }
//                     for (auto &I : bb) {
//                         if (auto *CI = dyn_cast<CallInst>(&I)) {
//                             if(MDNode *MD = CI->getMetadata("bb_index")) {
//                                 if (llvm::Metadata* md = MD->getOperand(0)) {
//                                     if (auto* mdv = llvm::dyn_cast<llvm::ConstantAsMetadata>(md)) {
//                                         if (llvm::ConstantInt* ci = llvm::dyn_cast<llvm::ConstantInt>(mdv->getValue())) {
//                                             uint64_t bbIndexValue = ci->getZExtValue();
//                                             ToggleInfo << bbIndexValue << " ";
//                                         }

//                                     }
//                                 }
//                             }
//                         }
//                     }
//                 }
//             }
//         }
//         b++;
//         total_reachable += reachable;
//         ToggleInfo << "\n"; 
//     }

//     instrumentUnreachableFuncs(*module, total_allowlist);
    

//     auto end = std::chrono::high_resolution_clock::now();
//     std::chrono::duration<double> duration = end - start;

//     std::cout <<"\n ****** Total Static Analysis Execution Time: " << duration.count() << "******\n";
//     myfile.close();
//     ToggleInfo.close();

//     std::cout <<"\n ****** [Total Optimized Target Points] : " << b << " ******\n";
//     int pruned_bb = allbbcount - (total_reachable);
    
//     double pruning_rate = std::max(0.0,((double)pruned_bb / (double)allbbcount) * 100);
//     std::cout<<"\n ****** [All Basic Blocks] : " << allbbcount << " ******\n";
//     std::cout<<"\n ****** [Total Reachable Blocks] : " << total_reachable << " ******\n";
//     std::cout <<"\n ****** [Pruning Rate] : " << pruning_rate << "% ******\n";
//     writeModuleToFile(module, "Mode1.ll");


// }



// void RustGoBasicPruningAnalysis(SVFModule* svfModule, Module* module, const SVFG* svfg, ICFG* icfg, PTACallGraph* callgraph, int allbbcount, std::set<const BasicBlock*>& allBasicBlocks)
// {
//     std::cout << "\n ****** Start Rust-Unaware Basic Pruning Analysis ******\n";
//     ofstream myfile;
//     myfile.open("BasicPruning.txt", ios::out | ios::app);
//     ofstream ToggleInfo;
//     ToggleInfo.open("ToggleInfo.txt", ios::out | ios::app);


//     /* Get all reachable functions from the Fuzz Entry Function */
//     const Function* mainFuzzEntry = get_function(svfModule);
//     const BasicBlock& mainBB = mainFuzzEntry->front();
//     const Instruction *mainInst = &mainBB.front();
//     const ICFGNode* mainNode = icfg->getBlockICFGNode(mainInst);

//     const SVFFunction* svfMain = svfModule->getSVFFunction(mainFuzzEntry);
//     PTACallGraphNode* startNode = callgraph->getCallGraphNode(svfMain);
//     std::set<const llvm::Function *> reachableFunctions = callgraphTraverse(startNode);


//     /* Optmized Toggle Points ICFNode */
//     std::set<const ICFGNode*> OptimizedTogglePoints;
//     std::set<std::string> targetCallSites;  
//     std::set<const SVFGNode*> topObjs = S2FuzzRawPtrInstructions(module, svfg, callgraph, OptimizedTogglePoints, reachableFunctions, reachableFunctions, targetCallSites, reachableFunctions);
//     /* Raw Pointer Related Heap Objects */
//     std::set<const SVFGNode*> heapObjs;
//     std::set<const SVFGEdge*> heapvisited;

//     /* Raw Pointer Related Stack Objects */
//     std::set<const SVFGNode*> stackObjs;
//     std::set<const SVFGEdge*> stackvisited;

//     /* Toggle Points ICFNode */
//     std::set<const ICFGNode*> toggleNode;
//     /* Counting Stack/Heap Object related to the Raw Pointers */
//     int a = 0;
//     /* Measure Static Analysis Time*/
//     auto start = std::chrono::high_resolution_clock::now();


//     for(auto ii = topObjs.begin(); ii != topObjs.end(); ii++)
//     {
//         a++;
//     }

//     std::cout << "\n******" << "Total Allocation Sites : " << a << "******\n";
    
//     std::set<const SVFGNode*> heapNodes;
//     std::set<const SVFGNode*> stackNodes;

//     for(auto ii = topObjs.begin(); ii != topObjs.end(); ii++)
//     {
//         const SVFGNode* topPtr = *ii;  
//         HeapOrStackIdentification(topPtr, heapObjs, stackObjs, heapNodes, stackNodes);
//     }

//     std::cout << "\n ******Finished Identifying HeapOrStack Allocation Sites******\n";

//     std::set<std::string> outermostFunctionsName;
//     std::set<std::string> doubleDropfuncs;
//     for(auto hi = heapObjs.begin(); hi != heapObjs.end(); hi++)
//     {
//         const SVFGNode* heapObj = *hi;
//         MarkHeapAllocationSites(heapObj);
//         S2FuzzHeapAccessSites(heapObj, toggleNode, heapvisited, reachableFunctions, outermostFunctionsName, &doubleDropfuncs);
//     }

//     std::cout << "\n ******Finished Marking Heap Access Sites******\n";
//     for(auto si = stackObjs.begin(); si != stackObjs.end(); si++)
//     {
//         const SVFGNode* stackObj = *si;
//         MarkStackAllocationSites(stackObj);
//         S2FuzzStackAccessSites(stackObj, toggleNode, stackvisited);
//     }
//     std::cout << "\n ******Finished Marking Stack Access Sites******\n";    

//     a = 0;


//     for(auto ti = toggleNode.begin(); ti != toggleNode.end(); ti++)
//     {
        
//         const ICFGNode* iNode = *ti;
//         const SVFFunction* svfFun = iNode->getFun();
//         if(svfFun) {
//             const Function* func = iNode->getFun()->getLLVMFun();
//             // If the function is not in the reachable functions, then it is not a toggle point
//             if(reachableFunctions.find(func) == reachableFunctions.end()) continue;
            

//             if(OptimizedTogglePoints.find(iNode) == OptimizedTogglePoints.end()) {
//                 a++;
//                 OptimizedTogglePoints.insert(iNode);
//             }

//         }
        
//     }

//     std::cout <<"\n ****** [Total Toggle Points] : " << a << " ******\n";

//     std::set<const BasicBlock*> withinSameBB;
//     // std::set<const Function*> withinSameFunc;
//     int total_reachable = 0;


//     int b = 0;
//     std::cout <<"\n ****** Start Rust-Unaware Backward Reachability Analysis ******\n";
//     for(auto ti = OptimizedTogglePoints.begin(); ti != OptimizedTogglePoints.end(); ti++)
//     {

//         const ICFGNode* target = *ti;
//         int reachable = 0;
//         std::set<const BasicBlock*> reachableBB;
//         std::set<const Function*> reachableFuncs;
//         std::set<std::string> allowlist;

//         const SVFFunction* svfFun = target->getFun();
//         if(svfFun) {
//             const Function* func = target->getFun()->getLLVMFun();
//             if(isDroplisted(func)) continue;
//             if(isBlacklisted(func) && !isCMLibrarylisted(func)) continue;
//         }

//         // /* If the toggle point is in the same basic block, skip analyzing */
//         const BasicBlock* targetBB = target->getBB();
//         if(targetBB) {
//             if(withinSameBB.find(targetBB) != withinSameBB.end()) continue;
//             withinSameBB.insert(targetBB);
//         }
//         traverseBackwardsFlowSensitiveMode1(icfg, target, callgraph, allowlist, reachableFuncs);

//         ToggleInfo << "[" << b <<"] : ";
//         for(auto f = reachableFuncs.begin(); f != reachableFuncs.end(); f++)
//         {
//             const Function* allowFunc = *f;
//             if(!allowFunc->isDeclaration())
//             {
//                 for (auto &bb : *allowFunc) {
//                     reachable++;
//                     for (auto &I : bb) {
//                         if (auto *CI = dyn_cast<CallInst>(&I)) {
//                             if(MDNode *MD = CI->getMetadata("bb_index")) {
//                                 if (llvm::Metadata* md = MD->getOperand(0)) {
//                                     if (auto* mdv = llvm::dyn_cast<llvm::ConstantAsMetadata>(md)) {
//                                         if (llvm::ConstantInt* ci = llvm::dyn_cast<llvm::ConstantInt>(mdv->getValue())) {
//                                             uint64_t bbIndexValue = ci->getZExtValue();
//                                             ToggleInfo << bbIndexValue << " ";

//                                         }

//                                     }
//                                 }
//                             }
//                         }
//                     }
//                 }
//             }
//         }

//         ToggleInfo << "\n"; 
//         total_reachable += reachable;
//         b++;

//     }

//     auto end = std::chrono::high_resolution_clock::now();
//     std::chrono::duration<double> duration = end - start;

//     std::cout <<"\n ****** Total Static Analysis Execution Time: " << duration.count() << "******\n";
//     myfile.close();
//     ToggleInfo.close();

//     std::cout <<"\n ****** [Optimized Toggle Points] : " << b << " ******\n";
//     int pruned_bb = allbbcount - (total_reachable / b);
//     double pruning_rate = ((double)pruned_bb / (double)allbbcount) * 100;

//     std::cout<<"\n ****** [All Basic Blocks] : " << allbbcount << " ******\n";

//     std::cout<<"\n ****** [Average Reachable Blocks] : " << (total_reachable/b) << " ******\n";

//     std::cout <<"\n ****** [Pruning Rate] : " << pruning_rate << "% ******\n";

//     writeModuleToFile(module, "BasicPruning.ll");

// }


// // Ablation Module 1: No Target Optimization
// void AblationModule1(SVFModule* svfModule, Module* module, const SVFG* svfg, ICFG* icfg, PTACallGraph* callgraph, int allbbcount, std::set<const BasicBlock*>& allBasicBlocks)
// {

//     ofstream ToggleInfo;
//     ToggleInfo.open("ToggleInfo.txt", ios::out | ios::app);

//     std::cout << "\n ****** Start Ablation Module 1 ******\n";
//     std::set<const SVFGNode*> topObjs = SanitizerRawPtrInstructions(module, svfg, callgraph);

//     std::set<const llvm::Function *> reachableFunctions;
//     const Function* mainFuzzEntry;
  
//     mainFuzzEntry = get_function(svfModule);
//     const BasicBlock& mainBB = mainFuzzEntry->front();
//     const Instruction *mainInst = &mainBB.front();
//     const ICFGNode* mainNode = icfg->getBlockICFGNode(mainInst);

//     const SVFFunction* svfMain = svfModule->getSVFFunction(mainFuzzEntry);
//     PTACallGraphNode* startNode = callgraph->getCallGraphNode(svfMain);
//     reachableFunctions = callgraphTraverse(startNode);
//     // reachableFunctions = TraverseCFGReachableFunc(mainNode);
    
//     /* Optmized Toggle Points ICFNode */
//     std::set<const ICFGNode*> OptimizedTogglePoints;

//     /* Raw Pointer Related Heap Objects */
//     std::set<const SVFGNode*> heapObjs;
//     std::set<const SVFGEdge*> heapvisited;

//     /* Raw Pointer Related Stack Objects */
//     std::set<const SVFGNode*> stackObjs;
//     std::set<const SVFGEdge*> stackvisited;

//     /* Toggle Points ICFNode */
//     std::set<const ICFGNode*> toggleNode;

//     /* Counting Stack/Heap Object related to the Raw Pointers */
//     int a = 0;

//     /* Measure Static Analysis Time*/
//     auto start = std::chrono::high_resolution_clock::now();

//     for(auto ii = topObjs.begin(); ii != topObjs.end(); ii++)
//     {
//         a++;
//     }

//     std::cout << "\n******" << "Total Allocation Sites : " << a << "******\n";

//     std::set<const SVFGNode*> heapNodes;
//     std::set<const SVFGNode*> stackNodes;

//     for(auto ii = topObjs.begin(); ii != topObjs.end(); ii++)
//     {
//         const SVFGNode* topPtr = *ii;  
//         HeapOrStackIdentification(topPtr, heapObjs, stackObjs, heapNodes, stackNodes);
//     }

//     for(auto hi = heapObjs.begin(); hi != heapObjs.end(); hi++)
//     {
//         const SVFGNode* heapObj = *hi;
//         MarkHeapAllocationSites(heapObj);
//         AblationModule1_HeapAccessSites(heapObj, toggleNode, heapvisited);
//     }

//     std::cout << "\n ******Finished Marking Heap Access Sites******\n";

//     for(auto si = stackObjs.begin(); si != stackObjs.end(); si++)
//     {
//         const SVFGNode* stackObj = *si;
//         MarkStackAllocationSites(stackObj);
//         S2FuzzStackAccessSites(stackObj, toggleNode, stackvisited);
//     }

//     std::cout << "\n ******Finished Marking Stack Access Sites******\n";

//     a = 0;
//     for(auto ti = toggleNode.begin(); ti != toggleNode.end(); ti++)
//     {
//         const ICFGNode* iNode = *ti;
//         const SVFFunction* svfFun = iNode->getFun();
//         if(svfFun) {
//             const Function* func = iNode->getFun()->getLLVMFun();
//             if(reachableFunctions.find(func) == reachableFunctions.end()) continue;
//             if(OptimizedTogglePoints.find(iNode) == OptimizedTogglePoints.end()) {
//                 a++;
//                 OptimizedTogglePoints.insert(iNode);
//             }
//         }
//     }

//     std::cout <<"\n ****** [Total Toggle Points] : " << a << " ******\n";

//     int b = 0;

//     int total_reachable = 0;
//     for(auto ti = OptimizedTogglePoints.begin(); ti != OptimizedTogglePoints.end(); ti++)
//     {
//         const ICFGNode* target = *ti;
//         std::set<const Function*> RustStdLibFunc;
//         std::set<const ICFGNode*> checked_iNodes;
//         int reachable = 0;
//         std::set<const BasicBlock*> ToggleBB;
//         std::set<const BasicBlock*> ToggleSTDBB;

//         const SVFFunction* svfFun = target->getFun();
//         if(svfFun) {
//             const Function* func = target->getFun()->getLLVMFun();
//             if(isDroplisted(func)) continue;

//             for (auto &BB : *func) {
//                 if(allBasicBlocks.find(&BB) != allBasicBlocks.end()) {
//                     if(ToggleBB.find(&BB) == ToggleBB.end()) {
//                         ToggleBB.insert(&BB);
//                     }
//                 }
//             }

//         }

//         checked_iNodes = S2FuzzBackwardAnalysis(target, callgraph, mainFuzzEntry, RustStdLibFunc);
//         for(auto fi = RustStdLibFunc.begin(); fi != RustStdLibFunc.end(); fi++) {
//             const Function* func = *fi;
//             for (auto &BB : *func) {
//                 if(allBasicBlocks.find(&BB) != allBasicBlocks.end()) {
//                     if(ToggleSTDBB.find(&BB) == ToggleSTDBB.end()) {
//                         reachable++;
//                         ToggleSTDBB.insert(&BB);
//                     }
//                 }
//             }
//         }

//         if(checked_iNodes.empty()) continue;
//         ToggleInfo << "[" << b <<"] : ";
//         for(auto ci = checked_iNodes.begin(); ci != checked_iNodes.end(); ci++) {
//             const BasicBlock* bb = (*ci)->getBB();
//             if(bb) {
//                 if(ToggleBB.find(bb) == ToggleBB.end()) {
//                     reachable++;
//                     ToggleBB.insert(bb);
//                 }
//             }
//         }

//         for(auto &bb : ToggleBB) {

//             for (auto &I : *bb) {
//                 if (auto *CI = dyn_cast<CallInst>(&I)) {
//                     if(MDNode *MD = CI->getMetadata("bb_index")) {
//                         if (llvm::Metadata* md = MD->getOperand(0)) {
//                             if (auto* mdv = llvm::dyn_cast<llvm::ConstantAsMetadata>(md)) {
//                                 if (llvm::ConstantInt* ci = llvm::dyn_cast<llvm::ConstantInt>(mdv->getValue())) {
//                                     uint64_t bbIndexValue = ci->getZExtValue();
//                                     ToggleInfo << bbIndexValue << " ";

//                                 }

//                             }
//                         }
//                     }
//                 }
//             }
//         }

//         ToggleInfo << "\n";   
//         total_reachable+= reachable; 

//         b++;

//     }

//     std::cout <<"\n ****** Finished Backward Reachability Analysis ******\n";

//     auto end = std::chrono::high_resolution_clock::now();
//     std::chrono::duration<double> duration = end - start;

//     std::cout <<"\n ****** Total Static Analysis Execution Time: " << duration.count() << "******\n";
//     ToggleInfo.close();

//     std::cout <<"\n ****** [Optimized Toggle Points] : " << b << " ******\n";
//     int pruned_bb = allbbcount - (total_reachable/b);
//     double pruning_rate = ((double)pruned_bb / (double)allbbcount) * 100;

//     std::cout<<"\n ****** [All Basic Blocks] : " << allbbcount << " ******\n";

//     std::cout<<"\n ****** [Average Reachable Blocks] : " << total_reachable/b << " ******\n";

//     std::cout <<"\n ****** [Pruning Rate] : " << pruning_rate << "% ******\n";

//     writeModuleToFile(module, "AblationModule1.ll");

// }

// void AblationModule2(SVFModule* svfModule, Module* module, const SVFG* svfg, ICFG* icfg, PTACallGraph* callgraph, int allbbcount, std::set<const BasicBlock*>& allBasicBlocks) {

//     std::cout << "\n ****** Start Ablation Module 2 ******\n";
//     ofstream lineInfo;
//     lineInfo.open("lineInfo.txt", ios::out | ios::app);

//     ofstream ToggleInfo;
//     ToggleInfo.open("ToggleInfo.txt", ios::out | ios::app);

//     /* Get all reachable functions from the Fuzz Entry Function */
//     std::set<const llvm::Function *> reachableFunctions;
//     const Function* mainFuzzEntry;
//     mainFuzzEntry = get_function(svfModule);
//     const BasicBlock& mainBB = mainFuzzEntry->front();
//     const Instruction *mainInst = &mainBB.front();
//     const ICFGNode* mainNode = icfg->getBlockICFGNode(mainInst);

//     const SVFFunction* svfMain = svfModule->getSVFFunction(mainFuzzEntry);
//     PTACallGraphNode* startNode = callgraph->getCallGraphNode(svfMain);
//     reachableFunctions = callgraphTraverse(startNode);
//     // reachableFunctions = TraverseCFGReachableFunc(mainNode);
    
//     // Reachable user defined functions reachable from main, not standard library
//     std::set<const llvm::Function *> userDefinedFunctions;
//     for(auto fi = reachableFunctions.begin(); fi != reachableFunctions.end(); fi++) {
//         const Function* func = *fi;
//         if(func->isDeclaration() || (isBlacklisted(func) && !isCMLibrarylisted(func)) || isDroplisted(func)) continue;
//         userDefinedFunctions.insert(func);
//     }

//     // Reachable standard outermost functions reachable from main (these points can be target points)
//     std::set<const llvm::Function *> outermostFunctions;
//     for (const Function* userFunc : userDefinedFunctions) {
        
//         for (const BasicBlock &BB : *userFunc) {
//             for (const Instruction &I : BB) {
//                 if (const CallInst *callInst = dyn_cast<CallInst>(&I) ) {
//                     const Function* calledFunc = callInst->getCalledFunction();
//                     if(!calledFunc) continue;
//                     if(reachableFunctions.find(calledFunc) == reachableFunctions.end()) continue;
//                     if(userDefinedFunctions.find(calledFunc) != userDefinedFunctions.end()) continue;
//                     // std::string callingFunc = calledFunc->getName().str();
//                     if(outermostFunctions.find(calledFunc) == outermostFunctions.end()) {
//                         outermostFunctions.insert(calledFunc);
//                     }

//                 }

//                 else if (const InvokeInst *invokeInst = dyn_cast<InvokeInst>(&I)) {
//                     const Function* calledFunc = invokeInst->getCalledFunction();
//                     if(!calledFunc) continue;
//                     if(reachableFunctions.find(calledFunc) == reachableFunctions.end()) continue;
//                     if(userDefinedFunctions.find(calledFunc) != userDefinedFunctions.end()) continue;
//                     if(outermostFunctions.find(calledFunc) == outermostFunctions.end()) {
//                         outermostFunctions.insert(calledFunc);
//                     }

//                 }
//             }
//         }
//     }



//     std::set<std::string> outermostFunctionsName;
//     for(auto fi = outermostFunctions.begin(); fi != outermostFunctions.end(); fi++) {
//         const Function* func = *fi;
//         outermostFunctionsName.insert(func->getName().str());
//     }

//     /* Optmized Toggle Points ICFNode */
//     std::set<const ICFGNode*> OptimizedTogglePoints;
//     std::set<std::string> targetCallSites;

//     /* Raw Pointer Load/Store -> OptimizedTogglePoints && Allocation Sites stemming from Raw Pointers -> topObjs (from only reachable functions) */
//     std::set<const SVFGNode*> topObjs = S2FuzzRawPtrInstructions(module, svfg, callgraph, OptimizedTogglePoints, reachableFunctions, outermostFunctions, targetCallSites, userDefinedFunctions);
    

//     /* Raw Pointer Related Heap Objects */
//     std::set<const SVFGNode*> heapObjs;
//     std::set<const SVFGEdge*> heapvisited;

//     /* Raw Pointer Related Stack Objects */
//     std::set<const SVFGNode*> stackObjs;
//     std::set<const SVFGEdge*> stackvisited;

//     /* Toggle Points ICFNode */
//     std::set<const ICFGNode*> toggleNode;

//     /* Counting Stack/Heap Object related to the Raw Pointers */
//     int a = 0;
//     std::set<std::string> DoubleDropFuncs;

//     /* Measure Static Analysis Time*/
//     auto start = std::chrono::high_resolution_clock::now();

//     for(auto ii = topObjs.begin(); ii != topObjs.end(); ii++)
//     {
//         a++;
//     }

//     std::cout << "\n ******" << "Total Allocation Sites : " << a << "******\n";
    
//     std::set<const SVFGNode*> heapNodes;
//     std::set<const SVFGNode*> stackNodes;
    
    
//    /* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@Before Optimization@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */ 
//     for(auto ii = topObjs.begin(); ii != topObjs.end(); ii++)
//     {
//         const SVFGNode* topPtr = *ii;  
//         HeapOrStackIdentification(topPtr, heapObjs, stackObjs, heapNodes, stackNodes);
//     }
//    /* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@Before Optimization@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */ 


//    /* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@Before Optimization@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */ 
//     for(auto hi = heapObjs.begin(); hi != heapObjs.end(); hi++)
//     {
//         const SVFGNode* heapObj = *hi;
//         MarkHeapAllocationSites(heapObj);        
//         S2FuzzHeapAccessSites(heapObj, toggleNode, heapvisited, reachableFunctions, outermostFunctionsName, &DoubleDropFuncs);
//     }

//     std::cout << "\n ******Finished Marking Heap Access Sites******\n";

//     for(auto si = stackObjs.begin(); si != stackObjs.end(); si++)
//     {
//         const SVFGNode* stackObj = *si;
//         MarkStackAllocationSites(stackObj);
//         S2FuzzStackAccessSites(stackObj, toggleNode, stackvisited);
//     }

//     std::cout << "\n ******Finished Marking Stack Access Sites******\n";
    
//     for(auto ti = toggleNode.begin(); ti != toggleNode.end(); ti++)
//     {
        
//         const ICFGNode* iNode = *ti;
//         const SVFFunction* svfFun = iNode->getFun();
//         if(svfFun) {
//             const Function* func = iNode->getFun()->getLLVMFun();
//             // If the function is not in the reachable functions, then it is not a toggle point
//             if(reachableFunctions.find(func) == reachableFunctions.end()) continue;
            
//             // // If the function is in the standard library, then it should be performed with outermost functions
//             if(userDefinedFunctions.find(func) == userDefinedFunctions.end())
//             {
//                 if (targetCallSites.find(func->getName().str()) == targetCallSites.end()) {
//                     targetCallSites.insert(func->getName().str());
//                 }
//             }

//             // if(outermostFunctions.find(func) != outermostFunctions.end() && targetCallSites.find(func->getName().str()) == targetCallSites.end()) {
//             //     targetCallSites.insert(func->getName().str());
//             // }

//             // Else if the fnction is in the user defined functions, then it is a toggle point directly
//             else {
//                 if(OptimizedTogglePoints.find(iNode) == OptimizedTogglePoints.end()) {
//                     OptimizedTogglePoints.insert(iNode);
//                 }
//             }

//         }
        
//     }


//     for (auto ti = DoubleDropFuncs.begin(); ti != DoubleDropFuncs.end(); ti++) {
//         if (targetCallSites.find(*ti) == targetCallSites.end()) {
//             targetCallSites.insert(*ti);
//         }
//     }


//     for (const Function* userFunc : userDefinedFunctions) {
//         for (const BasicBlock &BB : *userFunc) {
//             for (const Instruction &I : BB) {
//                 if (const CallInst *callInst = dyn_cast<CallInst>(&I) ) {
//                     const Function* calledFunc = callInst->getCalledFunction();
//                     if(!calledFunc) continue;
//                     std::string callingFunc = calledFunc->getName().str();
//                     if(targetCallSites.find(callingFunc) != targetCallSites.end()) {

//                         Instruction *inst = const_cast<Instruction*>(&I);
//                         LLVMContext &context = inst->getContext();
//                         MDString *str = MDString::get(context, "stdCallSite");
//                         MDNode *node = MDNode::get(context, str);
//                         inst->setMetadata("stdCallSite", node);

//                         const ICFGNode* iNode = icfg->getBlockICFGNode(&I);
//                         if (iNode) {
//                             OptimizedTogglePoints.insert(iNode);
//                         }
//                     }

//                 }

//                 else if (const InvokeInst *invokeInst = dyn_cast<InvokeInst>(&I)) {
//                     const Function* calledFunc = invokeInst->getCalledFunction();
//                     if(!calledFunc) continue;
//                     std::string callingFunc = calledFunc->getName().str();
//                     if(targetCallSites.find(callingFunc) != targetCallSites.end()) {

//                         Instruction *inst = const_cast<Instruction*>(&I);
//                         LLVMContext &context = inst->getContext();
//                         MDString *str = MDString::get(context, "stdCallSite");
//                         MDNode *node = MDNode::get(context, str);
//                         inst->setMetadata("stdCallSite", node);

//                         const ICFGNode* iNode = icfg->getBlockICFGNode(&I);
//                         if (iNode) {
//                             OptimizedTogglePoints.insert(iNode);
//                         }
//                     }
//                 }


//             }
//         }
//     }

//     int b = 0;

//     std::set<const BasicBlock*> withinSameBB;
//     // std::set<const Function*> withinSameFunc;
//     int total_reachable = 0;

//     std::map<int, ToggleData> ToggleBasic;

//     std::cout <<"\n ****** Start Rust-Unaware Backward Reachability Analysis ******\n";

//     for(auto ti = OptimizedTogglePoints.begin(); ti != OptimizedTogglePoints.end(); ti++) {
//         const ICFGNode* target = *ti;
//         int reachable = 0;
//         std::set<const BasicBlock*> reachableBB;
//         std::set<const Function*> reachableFuncs;
//         std::set<std::string> allowlist;

//         const SVFFunction* svfFun = target->getFun();
//         if(svfFun) {
//             const Function* func = svfFun->getLLVMFun();
//             if(isDroplisted(func)) continue;

//         }

//         const BasicBlock* targetBB = target->getBB();
//         if(targetBB) {
//             if(withinSameBB.find(targetBB) != withinSameBB.end()) continue;
//             withinSameBB.insert(targetBB);
//         }
//         ToggleBasic[b].lineInfo = " ";

//         traverseBackwardsFlowSensitiveMode1(icfg, target, callgraph, allowlist, reachableFuncs);
//         ToggleInfo << "[" << b <<"] : ";
//         for(auto f = reachableFuncs.begin(); f != reachableFuncs.end(); f++)
//         {
//             const Function* allowFunc = *f;
//             if(!allowFunc->isDeclaration())
//             {
//                 for (auto &bb : *allowFunc) {
//                     reachable++;
//                     for (auto &I : bb) {
//                         if (auto *CI = dyn_cast<CallInst>(&I)) {
//                             if(MDNode *MD = CI->getMetadata("bb_index")) {
//                                 if (llvm::Metadata* md = MD->getOperand(0)) {
//                                     if (auto* mdv = llvm::dyn_cast<llvm::ConstantAsMetadata>(md)) {
//                                         if (llvm::ConstantInt* ci = llvm::dyn_cast<llvm::ConstantInt>(mdv->getValue())) {
//                                             uint64_t bbIndexValue = ci->getZExtValue();
//                                             ToggleInfo << bbIndexValue << " ";
//                                             ToggleBasic[b].BBIds.insert(bbIndexValue);

//                                         }

//                                     }
//                                 }
//                             }
//                         }
//                     }
//                 }
//             }
//         }

//         ToggleInfo << "\n"; 
//         total_reachable += reachable;
//         b++;
//     }

//     std::map<int, ToggleData> ToggleCondensed;
//     std::set<std::set<uint64_t>> unique_values;
//     int condensed_count = 0;

//     for (const auto& pair : ToggleBasic) {
//         const int& key = pair.first;
//         const ToggleData& value = pair.second;

//         if(unique_values.find(value.BBIds) == unique_values.end()) {
//             ToggleCondensed[condensed_count++] = value;
//             unique_values.insert(value.BBIds);
//         }
//     }

//     ofstream fDebug1;
//     fDebug1.open("AblationModule2_ToggleCondensed.txt", ios::out | ios::trunc);
//     for(auto &ti : ToggleCondensed) {
//         fDebug1 << "[" << ti.first << "] : ";
//         for(auto &bb : ti.second.BBIds) {
//             fDebug1 << bb << " ";
//         }
//         fDebug1 << "\n";
//     }
//     fDebug1.close();


//     auto end = std::chrono::high_resolution_clock::now();
//     std::chrono::duration<double> duration = end - start;

//     std::cout <<"\n ****** Total Static Analysis Execution Time: " << duration.count() << "******\n";
//     ToggleInfo.close();

//     std::cout <<"\n ****** [Optimized Toggle Points] : " << b << " ******\n";
//     int pruned_bb = allbbcount - (total_reachable / b);
//     double pruning_rate = ((double)pruned_bb / (double)allbbcount) * 100;

//     std::cout<<"\n ****** [All Basic Blocks] : " << allbbcount << " ******\n";

//     std::cout<<"\n ****** [Average Reachable Blocks] : " << (total_reachable/b) << " ******\n";

//     std::cout <<"\n ****** [Pruning Rate] : " << pruning_rate << "% ******\n";

//     writeModuleToFile(module, "AblationModule2.ll");

// }




/* ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ */
// Ablation Study Modules

void AblationStudyModuleBase(SVFModule* svfModule, Module* module, const SVFG* svfg, ICFG* icfg, PTACallGraph* callgraph, int allbbcount, std::set<const BasicBlock*> allBasicBlocks, const std::vector<int>& targetNodes) 
{
    std::set<const ICFGNode*> TogglePoints;

    auto start = std::chrono::high_resolution_clock::now();

    for (int nodeId : targetNodes) {
        const ICFGNode* node = icfg->getICFGNode(nodeId);
        TogglePoints.insert(node);
    }

    std::set<const llvm::Function*> reachableFunctions;
    const Function* mainFuzzEntry;
    mainFuzzEntry = get_function(svfModule);
    const BasicBlock& mainBB = mainFuzzEntry->front();
    const Instruction* mainInst = &mainBB.front();
    const ICFGNode* mainNode = icfg->getBlockICFGNode(mainInst);

    const SVFFunction* svfMain = svfModule->getSVFFunction(mainFuzzEntry);
    PTACallGraphNode* startNode = callgraph->getCallGraphNode(svfMain);
    reachableFunctions = callgraphTraverse(startNode);

    std::set<std::string> total_allowlist;


    std::set<const Function*> total_allowlist_funcs;
    std::set<const BasicBlock*> total_allowlist_BBs;

    std::set<const Function*> source_allowlist_funcs;
    std::set<const BasicBlock*> source_allowlist_BBs;

    std::set<const Function*> std_allowlist_funcs;
    std::set<const BasicBlock*> std_allowlist_BBs;

    int b = 0;
    int total_bb_count = 0;

    for(auto ti = TogglePoints.begin(); ti != TogglePoints.end(); ti++) {
        const ICFGNode* target = *ti;
        std::set<std::string> individual_allowlist;
        std::set<const Function*> individual_allowlist_funcs;
        std::set<const BasicBlock*> individual_allowlist_BBs;
        std::set<const Function*> individual_source_allowlist_funcs;
        std::set<const BasicBlock*> individual_source_allowlist_BBs;

        std::set<const Function*> individual_std_allowlist_funcs;
        std::set<const BasicBlock*> individual_std_allowlist_BBs;

        traverseBackwardsFlowSensitiveForRust(icfg, target, callgraph, individual_allowlist);
        for(auto f = individual_allowlist.begin(); f != individual_allowlist.end(); f++) {
            total_allowlist.insert(*f);
        }
        

        for (auto &func : *module) {
            if (func.isDeclaration()) continue;
            if (individual_allowlist.find(func.getName().str()) != individual_allowlist.end()) {
                individual_allowlist_funcs.insert(&func);
                
                if(isDroplisted(&func) || isBlacklisted(&func)) {
                    individual_std_allowlist_funcs.insert(&func);
                    for (auto &bb : func) {
                        individual_std_allowlist_BBs.insert(&bb);
                    }
                }
                else {
                    individual_source_allowlist_funcs.insert(&func);
                    for (auto &bb : func) {
                        individual_source_allowlist_BBs.insert(&bb);
                    }
                }

                for (auto &bb : func) {
                    individual_allowlist_BBs.insert(&bb);
                }

            }
        }

        std::cout << "Toggle Point [" << b << "] - Allowlist Function count : " << individual_allowlist_funcs.size() << "\n";
        std::cout << "Toggle Point [" << b << "] - ***Allowlist BB count*** : " << individual_allowlist_BBs.size() << "\n";
        std::cout << "Toggle Point [" << b << "] - Source Allowlist Function count : " << individual_source_allowlist_funcs.size() << "\n";
        std::cout << "Toggle Point [" << b << "] - Source Allowlist BB count : " << individual_source_allowlist_BBs.size() << "\n";
        std::cout << "Toggle Point [" << b << "] - STD Allowlist Function count : " << individual_std_allowlist_funcs.size() << "\n";
        std::cout << "Toggle Point [" << b << "] - STD Allowlist BB count : " << individual_std_allowlist_BBs.size() << "\n";
        std::cout << "\n";

        total_bb_count += individual_allowlist_BBs.size();
        b++;

    }

    std::cout << "\n\n";
    int average_allowlist_bb_count = 0;
    if (b > 0) {
        average_allowlist_bb_count = total_bb_count / b;
    }
    std::cout << "!![Ablation Base] Average Allowlist BB count!! : " << average_allowlist_bb_count << "\n\n";


    instrumentUnreachableFuncs(*module, total_allowlist);

    for (auto &func : *module) {
        if (func.isDeclaration()) continue;
        if (total_allowlist.find(func.getName().str()) != total_allowlist.end()) {
            total_allowlist_funcs.insert(&func);

            if(isDroplisted(&func) || isBlacklisted(&func)) {
                std_allowlist_funcs.insert(&func);
                for (auto &bb : func) {
                    std_allowlist_BBs.insert(&bb);
                }
            }
            else {
                source_allowlist_funcs.insert(&func);
                for (auto &bb : func) {
                    source_allowlist_BBs.insert(&bb);
                }
            }
            
            for (auto &bb : func) {
                total_allowlist_BBs.insert(&bb);
            }
        }
    }

    std::cout << "\n";
    std::cout <<"\n ****** [Total Target Points] : " << b << " ******\n";


    std::cout << "[Ablation Base] Total Allowlist Function count : " << total_allowlist_funcs.size() << "\n";
    std::cout << "!![Ablation Base] Total Allowlist BB count!! : " << total_allowlist_BBs.size() << "\n\n";

    std::cout << "[Ablation Base] STD Allowlist Function count : " << std_allowlist_funcs.size() << "\n";
    std::cout << "[Ablation Base] STD Allowlist BB count : " << std_allowlist_BBs.size() << "\n\n";

    std::cout << "[Ablation Base] Source Allowlist Function count : " << source_allowlist_funcs.size() << "\n";
    std::cout << "[Ablation Base] Source Allowlist BB count : " << source_allowlist_BBs.size() << "\n\n";


    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;

    std::cout <<"\n ****** Total Static Analysis Execution Time: " << duration.count() << "******\n";
    
    writeModuleToFile(module, "AblationStudyModuleBase.ll");

}


void DisableStdLibOpt(SVFModule* svfModule, Module* module, const SVFG* svfg, ICFG* icfg, PTACallGraph* callgraph, int allbbcount, std::set<const BasicBlock*> allBasicBlocks, const std::vector<int>& targetNodes) 
{
    ofstream ToggleInfo;
    ToggleInfo.open("DisableStdLibOpt_ToggleInfo.txt", ios::out | ios::trunc);

    ofstream ToggleLineInfo;
    ToggleLineInfo.open("DisableStdLibOpt_ToggleLineInfo.txt", ios::out | ios::trunc);

    std::set<const ICFGNode*> TogglePoints;

    auto start = std::chrono::high_resolution_clock::now();

    for (int nodeId : targetNodes) {
        const ICFGNode* node = icfg->getICFGNode(nodeId);
        TogglePoints.insert(node);
    }

    std::set<const llvm::Function*> reachableFunctions;
    const Function* mainFuzzEntry;
    mainFuzzEntry = get_function(svfModule);
    const BasicBlock& mainBB = mainFuzzEntry->front();
    const Instruction* mainInst = &mainBB.front();
    const ICFGNode* mainNode = icfg->getBlockICFGNode(mainInst);

    const SVFFunction* svfMain = svfModule->getSVFFunction(mainFuzzEntry);
    PTACallGraphNode* startNode = callgraph->getCallGraphNode(svfMain);
    reachableFunctions = callgraphTraverse(startNode);

    int b = 0;
    int total_reachable = 0;

    std::map<int, ToggleData> ToggleBasic;
    for(auto ti = TogglePoints.begin(); ti != TogglePoints.end(); ti++) {
        const ICFGNode* target = *ti;
        int reachable = 0;

        const SVFFunction* svfFun = target->getFun();
        if(svfFun) {
            const Function* func = svfFun->getLLVMFun();
            if(isDroplisted(func)) continue;
        }

        std::set<const Function*> reachableFuncs;
        std::set<std::string> allowlist;

        traverseBackwardsFlowSensitiveMode1(icfg, target, callgraph, allowlist, reachableFuncs);
        
        
        /* ---------------------------------- Get Line Info ---------------------------------- */
        std::string lineInfoStr;
        if(const SVF::IntraBlockNode* intra = dyn_cast<SVF::IntraBlockNode>(target))
        {
            Instruction* I = const_cast<Instruction*>(intra->getInst());
            if (DILocation *Loc = I->getDebugLoc()) { 
                unsigned Line = Loc->getLine();
                StringRef File = Loc->getFilename();
                StringRef Directory = Loc->getDirectory();
                
                lineInfoStr = " (File: " + Directory.str() + "/" + File.str() + ", Line: " + std::to_string(Line) + ")";
            }  
            else {
                lineInfoStr = " (" + std::string("File: ") + "Unknown" + ", Line: " + "Unknown)";
            }  
        }
        else if(const SVF::CallBlockNode* callNode = dyn_cast<SVF::CallBlockNode>(target))
        {
            Instruction* I = const_cast<Instruction*>(callNode->getCallSite());
            if (DILocation *Loc = I->getDebugLoc()) { 
                unsigned Line = Loc->getLine();
                StringRef File = Loc->getFilename();
                StringRef Directory = Loc->getDirectory();
            
                lineInfoStr = " (File: " + Directory.str() + "/" + File.str() + ", Line: " + std::to_string(Line) + ")";
            } 
            else {
                lineInfoStr = " (" + std::string("File: ") + "Unknown" + ", Line: " + "Unknown)";
            }
        }
        else if(const SVF::RetBlockNode* RetNode = dyn_cast<SVF::RetBlockNode>(target))
        {
            Instruction* I = const_cast<Instruction*>(RetNode->getCallSite());
            if (DILocation *Loc = I->getDebugLoc()) { 
                unsigned Line = Loc->getLine();
                StringRef File = Loc->getFilename();
                StringRef Directory = Loc->getDirectory();
                
                lineInfoStr = std::string("File: ") + Directory.str() + "/" + File.str() + ", Line: " + std::to_string(Line);
            } 
            else {
                lineInfoStr = " (" + std::string("File: ") + "Unknown" + ", Line: " + "Unknown)";
            }
        }
        else {
            lineInfoStr = " (" + std::string("File: ") + "Unknown" + ", Line: " + "Unknown)";
        }
        ToggleBasic[b].lineInfo = lineInfoStr;
        /* ---------------------------------- Get Line Info ---------------------------------- */


        ToggleInfo << "[" << b << "] : ";
        for (auto f = reachableFuncs.begin(); f != reachableFuncs.end(); f++) {
            const Function* allowFunc = *f;
            if(!allowFunc->isDeclaration()) {
                for (auto &bb : *allowFunc) {
                    reachable++;
                    for (auto &I : bb) {
                        if (auto *CI = dyn_cast<CallInst>(&I)) {
                            if(MDNode *MD = CI->getMetadata("bb_index")) {
                                if (llvm::Metadata* md = MD->getOperand(0)) {
                                    if (auto* mdv = llvm::dyn_cast<llvm::ConstantAsMetadata>(md)) {
                                        if (llvm::ConstantInt* ci = llvm::dyn_cast<llvm::ConstantInt>(mdv->getValue())) {
                                            uint64_t bbIndexValue = ci->getZExtValue();
                                            ToggleInfo << bbIndexValue << " ";
                                            ToggleBasic[b].BBIds.insert(bbIndexValue);
                                        }

                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        ToggleInfo << "\n";
        total_reachable += reachable;
        b++;

    }


    std::map<int, ToggleData> ToggleCondensed;

    std::set<std::set<uint64_t>> unique_values;
    int condensed_count = 0;

    int basic_count = 0;
    for (const auto& pair : ToggleBasic) {
        const int& key = pair.first;
        const ToggleData& value = pair.second;
        ToggleLineInfo << "[" << key << "] : ";
        basic_count++;
        for(auto &bb : value.BBIds) {
            ToggleLineInfo << bb << " ";
        }
        ToggleLineInfo << value.lineInfo << "\n";

        if (unique_values.find(value.BBIds) == unique_values.end()) {
            ToggleCondensed[condensed_count++] = value;
            unique_values.insert(value.BBIds); 
        }
    }
    ToggleLineInfo.close();
    
    int c = 0;
    ofstream fDebug1;
    fDebug1.open("DisableStdLibOpt_ToggleCondensed.txt", ios::out | ios::trunc);
    for(auto &ti : ToggleCondensed) {
        fDebug1 << "[" << ti.first << "] : ";
        c++;
        for(auto &bb : ti.second.BBIds) {
            fDebug1 << bb << " ";
        }
        fDebug1 << "\n";
    }
    fDebug1.close();

    ofstream fDebug0;
    fDebug0.open("DisableStdLibOpt_ToggleCondensedLineInfo.txt", ios::out | ios::trunc);
    for(auto &ti : ToggleCondensed) {
        fDebug0 << "[" << ti.first << "] : ";
        for(auto &bb : ti.second.BBIds) {
            fDebug0 << bb << " ";
        }
        fDebug0 << ti.second.lineInfo << "\n";
    }
    fDebug0.close();
    
    ToggleLineInfo.close();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;
    ToggleInfo.close();

    std::cout <<"\n ****** Total Static Analysis Execution Time: " << duration.count() << "******\n";
    

    std::cout <<"\n ****** [Optimized Toggle Points] : " << b << " ******\n";
    int average_reachable = 0;
    if (b > 0) {
        average_reachable = total_reachable / b;
    }
    int pruned_bb = allbbcount - average_reachable;
    double pruning_rate = 0.0;
    if (allbbcount > 0) {
        pruning_rate = ((double)pruned_bb / (double)allbbcount) * 100;
    }

    std::cout<<"\n ****** [All Basic Blocks] : " << allbbcount << " ******\n";

    std::cout<<"\n ****** [Average Reachable Blocks] : " << average_reachable << " ******\n";

    std::cout <<"\n ****** [Pruning Rate] : " << pruning_rate << "% ******\n";

    writeModuleToFile(module, "DisableStdLibOpt.ll");

}




void NewVulAblationStudyModuleBase(SVFModule* svfModule, Module* module, const SVFG* svfg, ICFG* icfg, PTACallGraph* callgraph, size_t fuzzing_options, bool debug_pruning_rate, int allbbcount, std::set<const BasicBlock*>& allBasicBlocks)
{

    if(debug_pruning_rate) {
        std::cout << "\n ****** Static Analysis with Pruning Rate Information  ******\n";
    }
    else {
        std::cout << "\n ****** Static Analysis without Pruning Rate Information (for static time evaluation) ******\n";
    }
    
    /* Get all reachable functions from the Fuzz Entry Function */
    std::set<const llvm::Function *> reachableFunctions;
    const Function* mainFuzzEntry;
    /* fuzzing_options == 0 --> afl.rs */
    if(fuzzing_options == 0) {
        mainFuzzEntry = get_function(svfModule);
        const BasicBlock& mainBB = mainFuzzEntry->front();
        const Instruction *mainInst = &mainBB.front();
        const ICFGNode* mainNode = icfg->getBlockICFGNode(mainInst);

        const SVFFunction* svfMain = svfModule->getSVFFunction(mainFuzzEntry);
        PTACallGraphNode* startNode = callgraph->getCallGraphNode(svfMain);
        reachableFunctions = callgraphTraverse(startNode);
        // reachableFunctions = TraverseCFGReachableFunc(mainNode);
    }

    // Reachable user defined functions reachable from main, not standard library
    std::set<const llvm::Function *> userDefinedFunctions;
    for(auto fi = reachableFunctions.begin(); fi != reachableFunctions.end(); fi++) {
        const Function* func = *fi;
        if(func->isDeclaration() || (isBlacklisted(func) && !isCMLibrarylisted(func)) || isDroplisted(func)) continue;
        userDefinedFunctions.insert(func);
        // std::cout << "User Defined Function : " << func->getName().str() << "\n";
    }


    // Reachable standard outermost functions reachable from main (these points can be target points)
    std::set<const llvm::Function *> outermostFunctions;
    for (const Function* userFunc : userDefinedFunctions) {
        
        for (const BasicBlock &BB : *userFunc) {
            for (const Instruction &I : BB) {
                if (const CallInst *callInst = dyn_cast<CallInst>(&I) ) {
                    const Function* calledFunc = callInst->getCalledFunction();
                    if(!calledFunc) continue;
                    if(reachableFunctions.find(calledFunc) == reachableFunctions.end()) continue;
                    if(userDefinedFunctions.find(calledFunc) != userDefinedFunctions.end()) continue;
                    // std::string callingFunc = calledFunc->getName().str();
                    if(outermostFunctions.find(calledFunc) == outermostFunctions.end()) {
                        outermostFunctions.insert(calledFunc);
                    }

                }

                else if (const InvokeInst *invokeInst = dyn_cast<InvokeInst>(&I)) {
                    const Function* calledFunc = invokeInst->getCalledFunction();
                    if(!calledFunc) continue;
                    if(reachableFunctions.find(calledFunc) == reachableFunctions.end()) continue;
                    if(userDefinedFunctions.find(calledFunc) != userDefinedFunctions.end()) continue;
                    if(outermostFunctions.find(calledFunc) == outermostFunctions.end()) {
                        outermostFunctions.insert(calledFunc);
                    }

                }
            }
        }
    }



    std::set<std::string> outermostFunctionsName;
    for(auto fi = outermostFunctions.begin(); fi != outermostFunctions.end(); fi++) {
        const Function* func = *fi;
        outermostFunctionsName.insert(func->getName().str());
        // std::cout << "Outermost Function : " << func->getName().str() << "\n";
    }

    

    /* fuzzing_options == 1 --> cargo-fuzz */
    if(fuzzing_options == 1) {
        mainFuzzEntry = get_function(svfModule);
        const BasicBlock& mainBB = mainFuzzEntry->front();
        const Instruction *mainInst = &mainBB.front();
        const ICFGNode* mainNode = icfg->getBlockICFGNode(mainInst);

        // const SVFFunction* svfMain = svfModule->getSVFFunction(mainFuzzEntry);
        // PTACallGraphNode* startNode = callgraph->getCallGraphNode(svfMain);
        reachableFunctions = TraverseCFGReachableFunc(mainNode);
    }

    /* Optmized Toggle Points ICFNode */
    std::set<const ICFGNode*> OptimizedTogglePoints;
    std::set<std::string> targetCallSites;

    /* Raw Pointer Load/Store -> OptimizedTogglePoints && Allocation Sites stemming from Raw Pointers -> topObjs (from only reachable functions) */
    std::set<const SVFGNode*> topObjs = S2FuzzRawPtrInstructions(module, svfg, callgraph, OptimizedTogglePoints, reachableFunctions, outermostFunctions, targetCallSites, userDefinedFunctions);
    
    // for(auto ti = topObjs.begin(); ti != topObjs.end(); ti++) {
    //     errs() << "Top Object : " << (*ti)->toString() << "\n";
    // }
    
    /* Raw Pointer Related Heap Objects */
    std::set<const SVFGNode*> heapObjs;
    std::set<const SVFGEdge*> heapvisited;

    /* Raw Pointer Related Stack Objects */
    std::set<const SVFGNode*> stackObjs;
    std::set<const SVFGEdge*> stackvisited;

    /* Toggle Points ICFNode */
    std::set<const ICFGNode*> toggleNode;

    /* Counting Stack/Heap Object related to the Raw Pointers */
    int a = 0;

    
    // /* Count All Basic Blocks from Fuzz Entry Point */
    // int allbbcount = 0;
    // std::set<const BasicBlock*> allBasicBlocks = countAllBB(svfModule, icfg, &allbbcount, fuzzing_options);
    std::set<std::string> DoubleDropFuncs;

    


    /* Measure Static Analysis Time*/
    auto start = std::chrono::high_resolution_clock::now();

    for(auto ii = topObjs.begin(); ii != topObjs.end(); ii++)
    {
        a++;
    }

    std::cout << "\n ******" << "Total Allocation Sites : " << a << "******\n";
    
    std::set<const SVFGNode*> heapNodes;
    std::set<const SVFGNode*> stackNodes;
    
    
   /* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@Before Optimization@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */ 
    for(auto ii = topObjs.begin(); ii != topObjs.end(); ii++)
    {
        const SVFGNode* topPtr = *ii;  
        HeapOrStackIdentification(topPtr, heapObjs, stackObjs, heapNodes, stackNodes);
    }
   /* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@Before Optimization@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */ 


   /* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@Before Optimization@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */ 
    for(auto hi = heapObjs.begin(); hi != heapObjs.end(); hi++)
    {
        const SVFGNode* heapObj = *hi;
        MarkHeapAllocationSites(heapObj);        
        S2FuzzHeapAccessSites(heapObj, toggleNode, heapvisited, reachableFunctions, outermostFunctionsName, &DoubleDropFuncs);
    }
    int m = 0;
    for(auto &n : toggleNode) {
        m++;
    }
    std::cout << "\n ******Total heap toggle Nodes : " << m << "******\n";

    std::cout << "\n ******Finished Marking Heap Access Sites******\n";

    for(auto si = stackObjs.begin(); si != stackObjs.end(); si++)
    {
        const SVFGNode* stackObj = *si;
        MarkStackAllocationSites(stackObj);
        S2FuzzStackAccessSites(stackObj, toggleNode, stackvisited);
    }

    m = 0;
    for(auto &tn : toggleNode) {
        m++;
    }
    std::cout << "\n ******Total toggle Nodes : " << m << "******\n";

    int l = 0;
    for(auto & ho : heapObjs) {
        l++;
    }
    std::cout << "\n ******Total Heap Allocation Sites : " << l << "******\n";
    l = 0;
    for(auto & so : stackObjs) {
        l++;
    }
    std::cout << "\n ******Total Stack Allocation Sites : " << l << "******\n";

    std::cout << "\n ******Finished Marking Stack Access Sites******\n";
    


    std::set<std::string> total_allowlist;
    std::set<const Function*> total_allowlist_funcs;
    std::set<const BasicBlock*> total_allowlist_BBs;

    std::set<const Function*> source_allowlist_funcs;
    std::set<const BasicBlock*> source_allowlist_BBs;

    std::set<const Function*> std_allowlist_funcs;
    std::set<const BasicBlock*> std_allowlist_BBs;

    int b = 0;
    int total_bb_count = 0;



    for(auto ti = toggleNode.begin(); ti != toggleNode.end(); ti++)
    {
        
        const ICFGNode* iNode = *ti;
        const SVFFunction* svfFun = iNode->getFun();
        if(svfFun) {
            const Function* func = iNode->getFun()->getLLVMFun();
            // If the function is not in the reachable functions, then it is not a toggle point
            if(reachableFunctions.find(func) == reachableFunctions.end()) continue;
            
            // // If the function is in the standard library, then it should be performed with outermost functions
            if(userDefinedFunctions.find(func) == userDefinedFunctions.end())
            {
                if (targetCallSites.find(func->getName().str()) == targetCallSites.end()) {
                    targetCallSites.insert(func->getName().str());
                }
            }

            // if(outermostFunctions.find(func) != outermostFunctions.end() && targetCallSites.find(func->getName().str()) == targetCallSites.end()) {
            //     targetCallSites.insert(func->getName().str());
            // }

            // Else if the fnction is in the user defined functions, then it is a toggle point directly
            else {
                if(OptimizedTogglePoints.find(iNode) == OptimizedTogglePoints.end()) {
                    OptimizedTogglePoints.insert(iNode);
                }
            }

        }
        
    }


    for (auto ti = DoubleDropFuncs.begin(); ti != DoubleDropFuncs.end(); ti++) {
        if (targetCallSites.find(*ti) == targetCallSites.end()) {
            targetCallSites.insert(*ti);
        }
    }


    for (const Function* userFunc : userDefinedFunctions) {
        for (const BasicBlock &BB : *userFunc) {
            for (const Instruction &I : BB) {
                if (const CallInst *callInst = dyn_cast<CallInst>(&I) ) {
                    const Function* calledFunc = callInst->getCalledFunction();
                    if(!calledFunc) continue;
                    std::string callingFunc = calledFunc->getName().str();
                    if(targetCallSites.find(callingFunc) != targetCallSites.end()) {

                        Instruction *inst = const_cast<Instruction*>(&I);
                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "stdCallSite");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("stdCallSite", node);

                        const ICFGNode* iNode = icfg->getBlockICFGNode(&I);
                        if (iNode) {
                            OptimizedTogglePoints.insert(iNode);
                        }
                    }

                }

                else if (const InvokeInst *invokeInst = dyn_cast<InvokeInst>(&I)) {
                    const Function* calledFunc = invokeInst->getCalledFunction();
                    if(!calledFunc) continue;
                    std::string callingFunc = calledFunc->getName().str();
                    if(targetCallSites.find(callingFunc) != targetCallSites.end()) {

                        Instruction *inst = const_cast<Instruction*>(&I);
                        LLVMContext &context = inst->getContext();
                        MDString *str = MDString::get(context, "stdCallSite");
                        MDNode *node = MDNode::get(context, str);
                        inst->setMetadata("stdCallSite", node);

                        const ICFGNode* iNode = icfg->getBlockICFGNode(&I);
                        if (iNode) {
                            OptimizedTogglePoints.insert(iNode);
                        }
                    }
                }


            }
        }
    }


    int z = 0;
    for(auto i = OptimizedTogglePoints.begin(); i != OptimizedTogglePoints.end(); i++) {
        z++;
    }
    std::cout << "\n ******Total Optimized Toggle Points : " << z << "******\n";


   /* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@Before Optimization@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */ 


   /* @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@Before Optimization@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */ 

    std::cout <<"\n ****** Start Backward Reachability Analysis ******\n";
    for(auto ti = OptimizedTogglePoints.begin(); ti != OptimizedTogglePoints.end(); ti++)
    {
        const ICFGNode* target = *ti;
        std::set<std::string> individual_allowlist;
        std::set<const Function*> individual_allowlist_funcs;
        std::set<const BasicBlock*> individual_allowlist_BBs;
        std::set<const Function*> individual_source_allowlist_funcs;
        std::set<const BasicBlock*> individual_source_allowlist_BBs;

        std::set<const Function*> individual_std_allowlist_funcs;
        std::set<const BasicBlock*> individual_std_allowlist_BBs;

        traverseBackwardsFlowSensitiveForRust(icfg, target, callgraph, individual_allowlist);
        for(auto f = individual_allowlist.begin(); f != individual_allowlist.end(); f++) {
            total_allowlist.insert(*f);
        }
        

        for (auto &func : *module) {
            if (func.isDeclaration()) continue;
            if (individual_allowlist.find(func.getName().str()) != individual_allowlist.end()) {
                individual_allowlist_funcs.insert(&func);
                
                if(isDroplisted(&func) || isBlacklisted(&func)) {
                    individual_std_allowlist_funcs.insert(&func);
                    for (auto &bb : func) {
                        individual_std_allowlist_BBs.insert(&bb);
                    }
                }
                else {
                    individual_source_allowlist_funcs.insert(&func);
                    for (auto &bb : func) {
                        individual_source_allowlist_BBs.insert(&bb);
                    }
                }

                for (auto &bb : func) {
                    individual_allowlist_BBs.insert(&bb);
                }

            }
        }

        std::cout << "Toggle Point [" << b << "] - Allowlist Function count : " << individual_allowlist_funcs.size() << "\n";
        std::cout << "Toggle Point [" << b << "] - ***Allowlist BB count*** : " << individual_allowlist_BBs.size() << "\n";
        std::cout << "Toggle Point [" << b << "] - Source Allowlist Function count : " << individual_source_allowlist_funcs.size() << "\n";
        std::cout << "Toggle Point [" << b << "] - Source Allowlist BB count : " << individual_source_allowlist_BBs.size() << "\n";
        std::cout << "Toggle Point [" << b << "] - STD Allowlist Function count : " << individual_std_allowlist_funcs.size() << "\n";
        std::cout << "Toggle Point [" << b << "] - STD Allowlist BB count : " << individual_std_allowlist_BBs.size() << "\n";
        std::cout << "\n";

        total_bb_count += individual_allowlist_BBs.size();
        b++;  

    }


    std::cout << "\n\n";
    int average_allowlist_bb_count = 0;
    if (b > 0) {
        average_allowlist_bb_count = total_bb_count / b;
    }
    std::cout << "!![Ablation Base] Average Allowlist BB count!! : " << average_allowlist_bb_count << "\n\n";


    instrumentUnreachableFuncs(*module, total_allowlist);

    for (auto &func : *module) {
        if (func.isDeclaration()) continue;
        if (total_allowlist.find(func.getName().str()) != total_allowlist.end()) {
            total_allowlist_funcs.insert(&func);

            if(isDroplisted(&func) || isBlacklisted(&func)) {
                std_allowlist_funcs.insert(&func);
                for (auto &bb : func) {
                    std_allowlist_BBs.insert(&bb);
                }
            }
            else {
                source_allowlist_funcs.insert(&func);
                for (auto &bb : func) {
                    source_allowlist_BBs.insert(&bb);
                }
            }
            
            for (auto &bb : func) {
                total_allowlist_BBs.insert(&bb);
            }
        }
    }

    std::cout << "\n";
    std::cout <<"\n ****** [Total Target Points] : " << b << " ******\n";


    std::cout << "[Ablation Base] Total Allowlist Function count : " << total_allowlist_funcs.size() << "\n";
    std::cout << "!![Ablation Base] Total Allowlist BB count!! : " << total_allowlist_BBs.size() << "\n\n";

    std::cout << "[Ablation Base] STD Allowlist Function count : " << std_allowlist_funcs.size() << "\n";
    std::cout << "[Ablation Base] STD Allowlist BB count : " << std_allowlist_BBs.size() << "\n\n";

    std::cout << "[Ablation Base] Source Allowlist Function count : " << source_allowlist_funcs.size() << "\n";
    std::cout << "[Ablation Base] Source Allowlist BB count : " << source_allowlist_BBs.size() << "\n\n";


    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;

    std::cout <<"\n ****** Total Static Analysis Execution Time: " << duration.count() << "******\n";
    
    writeModuleToFile(module, "NewVulAblationStudyModuleBase.ll");


}
