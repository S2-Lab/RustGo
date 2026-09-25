//===- svf-ex.cpp -- A driver example of SVF-------------------------------------//
//
//                     SVF: Static Value-Flow Analysis
//
// Copyright (C) <2013->  <Yulei Sui>
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
//===-----------------------------------------------------------------------===//

/*
 // A driver program of SVF including usages of SVF APIs
 //
 // Author: Yulei Sui,
 */

#include "SVF-FE/LLVMUtil.h"
#include "Graphs/SVFG.h"
#include "Graphs/GenericGraph.h"
#include "WPA/Andersen.h"
#include "WPA/FlowSensitive.h"
#include "WPA/WPAPass.h"
#include "SVF-FE/PAGBuilder.h"
#include "Graphs/PAG.h"
#include "Util/Options.h"
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Transforms/Instrumentation/AddressSanitizer.h>
#include "llvm/Support/raw_ostream.h"
#include "DDA/DDAPass.h"
#include "DDA/ContextDDA.h"

#include "S2pidy/util.h"

#include "MemoryModel/PointerAnalysis.h"
#include "WPA/TypeAnalysis.h"
#include "llvm/IR/Dominators.h"

using namespace llvm;
using namespace std;
using namespace SVF;


#define DEBUG 1


// Specify the target function for directed fuzzing
static llvm::cl::opt<std::string> TargetFunction("f", llvm::cl::desc("Specify target function"), llvm::cl::value_desc("function name"));

static llvm::cl::opt<std::string> TargetBB("b", llvm::cl::desc("Specify target basic block"), llvm::cl::value_desc("basic block name"));


static llvm::cl::opt<std::string> TargetFilepath("fpath", llvm::cl::desc("Specify target filepath"), llvm::cl::value_desc("filepath"));

static llvm::cl::opt<unsigned> TargetLine("fline", llvm::cl::desc("Specify target line"), llvm::cl::value_desc("line"));

static llvm::cl::opt<std::string> InputFilename(cl::Positional,
        llvm::cl::desc("<input bitcode>"), llvm::cl::init("-"));

static llvm::cl::list<int> TargetNodes("t", llvm::cl::desc("Specify one or more ICFG Node IDs"), llvm::cl::ZeroOrMore, llvm::cl::CommaSeparated);

// Switch to find viable functions to target with SieveFuzz
static llvm::cl::opt<bool> GetFeasible("get-feasible", llvm::cl::init(false),
         llvm::cl::desc("Perform feasibility analysis on fuzz target")); 

// Test for real application

static llvm::cl::opt<bool> GetMain("get-main", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application")); 

static llvm::cl::opt<bool> DumpCG("dump-cg", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application")); 

static llvm::cl::opt<bool> DumpCFG("dump-cfg", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application")); 

static llvm::cl::opt<bool> DumpPAG("dump-PAG", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application")); 

static llvm::cl::opt<bool> DumpVFG("dump-VFG", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application")); 


static llvm::cl::opt<bool> DataAnalysis("data-analysis", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application")); 

static llvm::cl::opt<bool> DumpSVFG("dump-svfg", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application")); 

static llvm::cl::opt<bool> RustDebugLoc("RustDebugLoc", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application")); 

static llvm::cl::opt<bool> GetAllBB("GetAllBB", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application")); 

static llvm::cl::opt<bool> CallGraphTraverse("CallGraphTraverse", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application")); 


static llvm::cl::opt<bool> version_0("version0", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application")); 

static llvm::cl::opt<bool> version_1("version1", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application")); 

static llvm::cl::opt<bool> erasan("erasan", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application")); 

static llvm::cl::opt<bool> erasan_opt("erasan-opt", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application")); 

static llvm::cl::opt<bool> erasan_thread_safe("erasan-thread-safe", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application")); 

static llvm::cl::opt<bool> version_debug("version_debug", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application")); 

static llvm::cl::opt<bool> Reachable("Reachable", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application")); 

static llvm::cl::opt<bool> erasan_unsafe_reachability("erasan-unsafe-reachability", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application")); 

static llvm::cl::opt<bool> debug("debug", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application")); 

static llvm::cl::opt<bool> erasan_unsafe_opt("erasan-unsafe-opt", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application"));      

static llvm::cl::opt<bool> erasan_unsafe_preprocess("erasan-unsafe-preprocess", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application"));

static llvm::cl::opt<bool> afl_input_sharing_mode("afl-input-sharing-mode", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application"));

static llvm::cl::opt<bool> afl_no_input_sharing_mode("afl-no-input-sharing-mode", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application"));

static llvm::cl::opt<bool> afl_no_input_sharing_mode_async_safe("afl-no-input-sharing-mode-async-safe", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application"));

static llvm::cl::opt<bool> rustgo_no_safecode_feedback("rustgo-no-safecode-feedback", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application"));

static llvm::cl::opt<bool> afl_opt("afl-opt", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application"));

// static llvm::cl::opt<bool> mode_base("mode-base", llvm::cl::init(false),
//          llvm::cl::desc("Test Base Mode"));

// static llvm::cl::opt<bool> mode_1("mode-1", llvm::cl::init(false),
//          llvm::cl::desc("Test Target Optimized Version"));


// static llvm::cl::opt<bool> mode_basic_pruning("mode-basic-pruning", llvm::cl::init(false),
//          llvm::cl::desc("Test Target Optimized & Toggling & Fair Scheduling Version"));

static llvm::cl::opt<bool> ablation_module_base("ablation-module-base", llvm::cl::init(false),
         llvm::cl::desc("No target optimization"));

static llvm::cl::opt<bool> disable_std_lib_opt("disable-std-lib-opt", llvm::cl::init(false),
         llvm::cl::desc("Disable Standard Library Optimization"));

// static llvm::cl::opt<bool> ablation_module_2("ablation-module-2", llvm::cl::init(false),
//          llvm::cl::desc("No target specific pruning"));

static llvm::cl::opt<bool> libfuzzer("libfuzzer", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application"));

static llvm::cl::opt<bool> pruning_rate_information("pruning-rate-information", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application"));

static llvm::cl::opt<bool> static_time_evaluation("static-time-evaluation", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application"));

static llvm::cl::opt<bool> afl_manual_target_collection("afl-manual-target-collection", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application"));

static llvm::cl::opt<bool> afl_manual_analysis_input_sharing("afl-manual-analysis-input-sharing", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application"));

static llvm::cl::opt<bool> afl_manual_analysis_no_input_sharing("afl-manual-analysis-no-input-sharing", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application"));

static llvm::cl::opt<bool> afl_manual_analysis_async_safe("afl-manual-analysis-async-safe", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application"));

static llvm::cl::opt<bool> afl_forward_analysis("afl-forward-analysis", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application"));

static llvm::cl::opt<bool> newvul_ablation_module_base("newvul-ablation-module-base", llvm::cl::init(false),
         llvm::cl::desc("Test Static Analysis Tool for real application"));

/*!
 * An example to query alias results of two LLVM values
 */
AliasResult aliasQuery(PointerAnalysis* pta, Value* v1, Value* v2)
{
    return pta->alias(v1,v2);
}

/*!
 * An example to print points-to set of an LLVM value
 */
// std::string printPts(PointerAnalysis* pta, const Value* val)
// {

//     std::string str;
//     raw_string_ostream rawstr(str);

//     NodeID pNodeId = pta->getPAG()->getValueNode(val);
//     rawstr << "pNodeId: " <<pNodeId << "\n";


//     const PointsTo& pts = pta->getPts(pNodeId);
//     for (PointsTo::iterator ii = pts.begin(), ie = pts.end();
//             ii != ie; ii++)
//     {
//         rawstr << " " << *ii << " ";
//         PAGNode* targetObj = pta->getPAG()->getPAGNode(*ii);

//         if(targetObj->hasValue())
//         {
//             rawstr << "(" <<*targetObj->getValue() << ")\t ";
//         }

//     }

//     return rawstr.str();

// }

void RprintPts(SVFG* svfg, int pagnum) {

    std::cout << "---------start---------" << "\n";
    // NodeID pNodeId = svfg->getPAG()->getValueNode(val);
    // PAGNode* pagnode = svfg->getPAG()->getPAGNode(pNodeId);
    const PointsTo& pts = svfg->getPTA()->getPts(pagnum);

    std::cout << "From pointer in PAG : " << pagnum << "\n";


    std::cout << "Pointer in PAG [" << pagnum << "] " << "points to memory location ";
    for (PointsTo::iterator ii = pts.begin(), ie = pts.end();
            ii != ie; ii++)
    {
        PAGNode* Obj = svfg->getPAG()->getPAGNode(*ii); // memory location
        std::cout << "[" << Obj->getId() << "] ";
    }

    std::cout << "\n";
    std::cout << "Memory in SVFG \n";
    for (PointsTo::iterator ii = pts.begin(), ie = pts.end();
            ii != ie; ii++)
    {
        PAGNode* Obj = svfg->getPAG()->getPAGNode(*ii); // memory location
        for (PAGEdge::PAGEdgeSetTy::iterator it = Obj->getOutEdges().begin(), eit = Obj->getOutEdges().end();
                 it != eit; ++it)
        {
            PAGEdge* edge = *it;
            if(edge->getEdgeKind() == PAGEdge::Addr) {
                PAGNode* ObjPtr = edge->getDstNode();
                if(svfg->hasDefSVFGNode(ObjPtr)) {
                    const SVFGNode* svfgObj = svfg->getDefSVFGNode(ObjPtr);
                    std::cout << "[" << svfgObj->getId() << "] ";
                }
            }
        }
    }

    std::cout << "\n";
    for (PointsTo::iterator ii = pts.begin(), ie = pts.end(); ii != ie; ii++)
    {
        NodeID memID = svfg->getPAG()->getPAGNode(*ii)->getId();
        std::cout << "PAG Memory [" << memID << "]" << "is pointed by SVFG pointers ";
        const NodeSet& rev = svfg->getPTA()->getRevPts(*ii); // pointer from each memory
        for (NodeSet::const_iterator it = rev.begin(); it != rev.end(); ++it) {
            PAGNode* pagpointer = svfg->getPAG()->getPAGNode(*it);
            for (PAGEdge::PAGEdgeSetTy::iterator it = pagpointer->getOutEdges().begin(), eit = pagpointer->getOutEdges().end();
                it != eit; ++it)
            {
                PAGEdge* edge = *it;
                if(edge->getEdgeKind() == PAGEdge::Addr) {
                    PAGNode* p = edge->getDstNode();
                    if(svfg->hasDefSVFGNode(p)) {
                        const SVFGNode* svfgPtr = svfg->getDefSVFGNode(p);
                        std::cout << "["<<svfgPtr->getId() << "] ";
                    }
                }
                else {
                    if(svfg->hasDefSVFGNode(pagpointer)) {
                        const SVFGNode* svfgPtr = svfg->getDefSVFGNode(pagpointer);
                        std::cout << "[" << svfgPtr->getId() << "] ";
                    }
                }

            }

        }
        std::cout << "\n";
    }



}

void pointAnalysis(SVFG* svfg, const Value* val) {
    NodeID pNodeId = svfg->getPTA()->getPAG()->getValueNode(val);
    std::cout << "pnodeId : " << pNodeId << "\n";

    const PointsTo& pts = svfg->getPTA()->getPts(pNodeId);

    for (PointsTo::iterator ii = pts.begin(), ie = pts.end();
            ii != ie; ii++)
    {
        std::cout << "Mem IDs : " << (*ii) << "\n";
        NodeID mem = *ii;
        const NodeSet& rev = svfg->getPTA()->getRevPts(mem);
        for (NodeSet::const_iterator it = rev.begin(); it != rev.end(); ++it)
        {
            std::cout << "alias-pointer IDs : " << *it << "\n";
        }

    }


}



/*!
 * An example to query/collect all successor nodes from a ICFGNode (iNode) along control-flow graph (ICFG)
 */
void traverseOnICFG(ICFG* icfg, const Instruction* inst)
{
    ICFGNode* iNode = icfg->getBlockICFGNode(inst);
    FIFOWorkList<const ICFGNode*> worklist;
    Set<const ICFGNode*> visited;
    worklist.push(iNode);

    /// Traverse along VFG
    while (!worklist.empty())
    {
        const ICFGNode* vNode = worklist.pop();
        for (ICFGNode::const_iterator it = iNode->OutEdgeBegin(), eit =
                    iNode->OutEdgeEnd(); it != eit; ++it)
        {
            ICFGEdge* edge = *it;
            ICFGNode* succNode = edge->getDstNode();
            if (visited.find(succNode) == visited.end())
            {
                visited.insert(succNode);
                worklist.push(succNode);
            }
        }
    }
}



/*!
 * An example to query/collect all the uses of a definition of a value along value-flow graph (VFG)
 */
void traverseOnVFG(const SVFG* vfg, const Value* val)
{
    PAG* pag = PAG::getPAG();

    PAGNode* pNode = pag->getPAGNode(pag->getValueNode(val));
    std::cout << "startPAGNode : " << pNode->getId() << "\n";
    const VFGNode* vNode = vfg->getDefSVFGNode(pNode);
    std::cout << "traverseOnVFG : " << vNode->getId() << "\n";
    FIFOWorkList<const VFGNode*> worklist;
    Set<const VFGNode*> visited;
    worklist.push(vNode);

    /// Traverse along VFG
    while (!worklist.empty())
    {
        const VFGNode* vNode = worklist.pop();
        for (VFGNode::const_iterator it = vNode->OutEdgeBegin(), eit =
                    vNode->OutEdgeEnd(); it != eit; ++it)
        {
            VFGEdge* edge = *it;
            VFGNode* succNode = edge->getDstNode();
            if (visited.find(succNode) == visited.end())
            {
                visited.insert(succNode);
                std::cout << "traverseOnVFG : " << succNode->getId() << "\n";
                worklist.push(succNode);
            }
        }
    }
    // /// Collect all LLVM Values
    // for(Set<const VFGNode*>::const_iterator it = visited.begin(), eit = visited.end(); it!=eit; ++it)
    // {
    //     const VFGNode* node = *it;
    //     /// can only query VFGNode involving top-level pointers (starting with % or @ in LLVM IR)
    //     const PAGNode* pNode = vfg->getLHSTopLevPtr(node);
    //     if(pNode!=nullptr && pNode->hasValue()) {
    //         const Value* val = pNode->getValue();
    //         std::cout << "traverseOnVFG with values : " << val->getName().str() << "\n";
    //     }

    // }
}


static std::set<Function*> debugfuncs(PTACallGraphNode* startNode, ofstream& ofs)
{
    FIFOWorkList<PTACallGraphNode*> worklist;
    std::set<PTACallGraphNode*> visited;
    std::set<Function*> functions;

    worklist.push(startNode);

    Function* startFun = startNode->getFunction()->getLLVMFun();

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
                Function* candidate = dstFun->getLLVMFun();
                // Recheck;


                if(functions.find(candidate) == functions.end() && !(candidate->isDeclaration())) {
                    functions.insert(candidate);
                    ofs << "Function : " << candidate->getName().str() << "\n";
                }
                
                worklist.push(dstNode);
                visited.insert(dstNode);
                            
            }
        }

    }

    return functions;

}


static void debugforward(const SVFGNode* sNode, std::set<const ICFGNode*>& togglePoints, std::set<const SVFGEdge*> &visited, ofstream & myfile)
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
                myfile << "Debug Forward : " << nextNode->toString() << "\n";

                worklist.push(nextNode);
                visited.insert(edge);
            }


        }


    }

}


void debugdebug(SVFG* svfg, int num, std::set<const ICFGNode*>& togglePoints, std::set<const SVFGEdge*>& visited)
{
    const SVFGNode* sNode = svfg->getSVFGNode(num);
    ofstream myfile;
    myfile.open("debug.txt", ios::out | ios::app);

    FIFOWorkList<const SVFGNode*> worklist;
    worklist.push(sNode);

    
    while(!worklist.empty())
    {
        const SVFGNode* vNode = worklist.pop();
        // worklist.pop();
        // std::cout << vNode->toString() << "\n";

        for(SVFGNode::const_iterator it = vNode->OutEdgeBegin(), eit = vNode->OutEdgeEnd();
            it != eit; ++it)
            {
                SVFGEdge* edge = *it;
                SVFGNode* nextNode = edge->getDstNode();
                if(visited.find(edge) == visited.end())
                {
                    s32_t nKind = nextNode->getNodeKind();
                    myfile << nextNode->toString() << "\n";
                    myfile << nKind << "\n";

                    // s32_t nKind = nextNode->getNodeKind();
                    // if(nKind == VFGNode::AParm || nKind == VFGNode::FParm || nKind == VFGNode::APIN || nKind == VFGNode::FPIN) {
                        
                    //     // myfile << "Parm : " << nextNode->toString() << "\n";
                    //     // const ICFGNode* iNode = nextNode->getICFGNode();
                    //     string strstr = nextNode->getFun()->getLLVMFun()->getName().str();
                    //     if(strstr == "_Z13drop_in_placePi") {
                    //         myfile << "From : " << nextNode->toString() << "\n";
                    //         myfile << "start debugforward \n";
                    //         debugforward(nextNode, togglePoints, visited, myfile);
                    //         myfile << "\n\n";
                    //     }
                        

                    // }
                    

                    worklist.push(nextNode);
                    visited.insert(edge);
                }
            }

    }
    myfile.close();

}



int allBBcount(Module & M) {
    std::set<const Function*> std_funcs;
    std::set<const BasicBlock*> std_BB;

    int count = 0;
    for(auto &function : M) {
        if(isBlacklisted(&function) || isDroplisted(&function)) {
            if(std_funcs.find(&function) == std_funcs.end()) {
                std_funcs.insert(&function);
            }
            for (auto &bb : function) {
                std_BB.insert(&bb);
            }
        }

        for(auto &bb : function) {
            count++;
        }
    }

    int j = 0;

    for(const auto & bb : std_BB) {
        j++;
    }

    std::cout << "[WHOLE MODULE] ALL BB count : [" << count << "]" << "\n";
    std::cout << "[WHOLE MODULE] STD BB count : [" << j << "]" << "\n";
    std::cout << "[WHOLE MODULE] Only Source Code BB count : [" << count-j << "]" << "\n";

    return count;

}

void findPossibleIndirectCallTargetsSVF(Module& M, PointerAnalysis* pta, PTACallGraph* callgraph, SVFModule* svfModule) {
    PAG* pag = pta->getPAG();

    for (Function& F : M) {
        // if (F.getName() != "_ZN4core4task4wake5Waker4wake17h16d98b0994a85095E") {
        //     continue;
        // }

        // Now, use the call graph to find possible callees
        const SVFFunction* svfCallerFunc = svfModule->getSVFFunction(&F);
        PTACallGraphNode* cgCallerNode = callgraph->getCallGraphNode(svfCallerFunc);

        if (!cgCallerNode) {
            // std::cerr << "Call graph node for function " << F.getName().str() << " not found.\n";
            continue;
        }

        std::set<const Function*> functions = callgraphTraverse(cgCallerNode);
        // for(const Function* func : functions) {
        //     std::cout << "Possible callee from call graph: " << func->getName().str() << "\n";
        // }
        for (BasicBlock& BB : F) {
            for (Instruction& I : BB) {
                if (CallBase* callInst = dyn_cast<CallBase>(&I)) {
                    if (!callInst->getCalledFunction()) {
                        // std::cout << "Indirect call detected\n";
                        // std::cout << "Func: " << F.getName().str() << "\n";
                        // std::cout << "BB: " << BB.getName().str() << "\n";

                        // Get the called value
                        Value* calledValue = callInst->getCalledOperand()->stripPointerCasts();
                        // std::cout << "Called Value: " << calledValue->getName().str() << "\n";

                        // Use PTA to get points-to set
                        if (pag->hasValueNode(calledValue)) {
                            NodeID valNode = pag->getValueNode(calledValue);
                            // std::cout << "Val Node : " << valNode << "\n";
                            
                            const PointsTo& pts = pta->getPts(valNode);
                            // std::cout << "Indirect call in function " << F.getName().str() << " at instruction: " << I.getDebugLoc().getLine() << "\n";
                            // std::cout << "Possible targets (SVF PTA):\n";
                            for (NodeID targetNode : pts) {
                                PAGNode* targetNodePtr = pag->getPAGNode(targetNode);
                                const Value* targetValue = targetNodePtr->getValue();
                                // if (const Function* targetFunc = dyn_cast<Function>(targetValue)) {
                                //     std::cout << "  " << targetFunc->getName().str() << "\n";
                                // }
                            }
                        }

                    }
                }
            }
        }
    }
}



int main(int argc, char ** argv)
{

    int arg_num = 0;
    char **arg_value = new char*[argc];
    string llvmModuleName;
    std::vector<std::string> moduleNameVec;
    SVFUtil::processArguments(argc, argv, arg_num, arg_value, moduleNameVec);
    cl::ParseCommandLineOptions(arg_num, arg_value,
                                "Whole Program Points-to Analysis\n");
    
    if (Options::WriteAnder == "ir_annotator")
    {
        LLVMModuleSet::getLLVMModuleSet()->preProcessBCs(moduleNameVec);
    }

    SVFModule* svfModule = LLVMModuleSet::getLLVMModuleSet()->buildSVFModule(moduleNameVec);

    svfModule->buildSymbolTableInfo();
    // svfModule->getModuleIdentifier();

	/// Build Program Assignment Graph (PAG)
	PAGBuilder builder;
	PAG* pag = builder.build(svfModule);


    /// Create Andersen's pointer analysis
    Andersen* ander = AndersenWaveDiff::createAndersenWaveDiff(pag);
    
    // ander->analyze();
    // TypeAnalysis* pta = new TypeAnalysis(pag);
    // pta->analyze();


    // ander->analyze();
    // ander->setFieldSensitive();
    // ander->setAnalysisKind(PTA::AndersenContextSensitive);

    /// Call Graph
    PTACallGraph* callgraph = ander->getPTACallGraph();

    /// ICFG
    ICFG* icfg = pag->getICFG();
    icfg->updateCallGraph(callgraph);
    // Later Implement -> Update ICFG with indirect call resolution
    bool indirect = false;
    if (indirect) {
        icfg->updateCallGraph(callgraph);
    }

    LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
    Module *llvmModule = ModSet->getMainLLVMModule(); 
    // findPossibleIndirectCallTargetsSVF(*llvmModule, ander, callgraph, svfModule);

    /// Value-Flow Graph (VFG)
    VFG* vfg = new VFG(callgraph);
    // vfg->dump("vfg");


    /// Sparse value-flow graph (SVFG)
    SVFGBuilder svfBuilder;
    SVFG* svfg = svfBuilder.buildFullSVFGWithoutOPT(ander);

    if(DumpVFG) {
        vfg->dump("vfg");
    }

    if(DumpPAG) {
        pag->dump("pag");
    }

    if(DumpCG) {
        callgraph->dump("callgraph");
    }

    if(DumpCFG) {
        icfg->updateCallGraph(callgraph);
        icfg->dump("icfg");
    }

    if(GetMain) {
        cout << "start get main " << "\n\n";
        getmain(svfModule);
    }
    if(DumpSVFG) {
        svfg->dump("svfg");
    }


    if(DataAnalysis) {
        std::string target = TargetFunction;
        int n = stoi(target);
        RprintPts(svfg, n);
    }


    if(GetAllBB) {
        int a = 0;
        countAllBB(svfModule, icfg, &a, 0);
        LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
        Module *llvmModule = ModSet->getMainLLVMModule(); 
        std::cout << "Main BB count : " << a << "\n";
        std::cout << "All BB count : " << allBBcount(*llvmModule) << "\n";

    }


    if(CallGraphTraverse) {
        const Function* main = get_function(svfModule);
        const SVFFunction* svfMain = svfModule->getSVFFunction(main);
        PTACallGraphNode* startNode = callgraph->getCallGraphNode(svfMain);
        callgraphTraverse(startNode);
    }


    if(version_debug) {
        // std::string target = TargetFunction;
        // int n = stoi(target);
        // staticAnalysis_Debug(svfModule, svfg, icfg, callgraph);
    }

    
    if(version_0){
        std::string target = TargetFunction;
        int n = stoi(target);
        // backwardAnalysis_V0(n, icfg, callgraph);
        forwardAnalysis_V0(n, svfg, callgraph);
    }


    if(version_1){
        LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
        Module *llvmModule = ModSet->getMainLLVMModule();
        staticAnalysis_V1(svfModule, llvmModule, svfg, icfg, callgraph);
        // RawPtrInstructions(svfModule, icfg);
    }


    if(Reachable) {
        const SVFFunction* srcFn = get_svf_function("_ZN3png6common15Transformations4bits17hef93064e81c662bbE", svfModule);
        const SVFFunction* dstFn = get_svf_function("_ZN153_$LT$core..result..Result$LT$T$C$F$GT$$u20$as$u20$core..ops..try_trait..FromResidual$LT$core..result..Result$LT$core..convert..Infallible$C$E$GT$$GT$$GT$13from_residual17h6457c36cc41346ddE", svfModule);
        bool reach = isReachableForRust(srcFn, dstFn, callgraph);
        if(reach) {
            std::cout << "Is reachable\n";
        }
        else {
            std::cout << "Is not reachable\n";
        }
    }

    if(erasan) {
        LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
        Module *llvmModule = ModSet->getMainLLVMModule();
        ErasanAnalysis(svfModule, llvmModule, svfg, icfg, callgraph);
    }

    if(erasan_opt) {
        LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
        Module *llvmModule = ModSet->getMainLLVMModule(); 
        ErasanAnalysis_opt(svfModule, llvmModule, svfg, icfg, callgraph);       
    }

    if(erasan_thread_safe) {
        LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
        Module *llvmModule = ModSet->getMainLLVMModule(); 
        RustThreadMarking(*llvmModule);
        ErasanAnalysis_thread_safe(svfModule, llvmModule, svfg, icfg, callgraph);       
    }

    if(erasan_unsafe_preprocess) {
        LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
        Module *llvmModule = ModSet->getMainLLVMModule(); 
        UnsafePreprocess(llvmModule);
    }

    if(erasan_unsafe_reachability) {
        LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
        Module *llvmModule = ModSet->getMainLLVMModule(); 
        ErasanAnalysis_unsafe_reachabilty(svfModule, llvmModule, svfg, icfg, callgraph);       
    }

    if(erasan_unsafe_opt) {
        LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
        Module *llvmModule = ModSet->getMainLLVMModule();
        ErasanAnalysis_unsafe_opt(svfModule, llvmModule, svfg, icfg, callgraph);
    }


    if(debug) {
        LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
        Module *llvmModule = ModSet->getMainLLVMModule(); 
        countAllBB_debug(svfModule, llvmModule, icfg, 0);

    }

    if(afl_input_sharing_mode) {

        bool debug_pruning_rate = false;

        if(pruning_rate_information) {
            debug_pruning_rate = true;
        }

        if(static_time_evaluation) {
            debug_pruning_rate = false;
        }

        RustStdLibrariesMarking(*llvmModule, svfModule, callgraph);
        RustUnsafeFuncsMarking(*llvmModule);

        int allbbcount = 0;
        std::set<const BasicBlock*> allBasicBlocks = countAllBB(svfModule, icfg, &allbbcount, 0);
        

        // LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
        // Module *llvmModule = ModSet->getMainLLVMModule(); 

        allbbcount = allBBcount(*llvmModule);
        
        setTogglePointsInputSharingMode(*llvmModule);
        S2FuzzStaticAnalysis(svfModule, llvmModule, svfg, icfg, callgraph, 0, debug_pruning_rate, allbbcount, allBasicBlocks);
    }

    if(afl_no_input_sharing_mode) {

        bool debug_pruning_rate = false;

        if(pruning_rate_information) {
            debug_pruning_rate = true;
        }

        if(static_time_evaluation) {
            debug_pruning_rate = false;
        }

        RustStdLibrariesMarking(*llvmModule, svfModule, callgraph);
        RustUnsafeFuncsMarking(*llvmModule);

        int allbbcount = 0;
        std::set<const BasicBlock*> allBasicBlocks = countAllBB(svfModule, icfg, &allbbcount, 0);
        
        // LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
        // Module *llvmModule = ModSet->getMainLLVMModule(); 

        allbbcount = allBBcount(*llvmModule);
        
        setTogglePoints(*llvmModule);
        S2FuzzStaticAnalysis(svfModule, llvmModule, svfg, icfg, callgraph, 0, debug_pruning_rate, allbbcount, allBasicBlocks);
    }

    if(afl_no_input_sharing_mode_async_safe) {

        bool debug_pruning_rate = false;

        if(pruning_rate_information) {
            debug_pruning_rate = true;
        }

        if(static_time_evaluation) {
            debug_pruning_rate = false;
        }

        RustStdLibrariesMarking(*llvmModule, svfModule, callgraph);
        RustAsyncFuncsMarking(*llvmModule);
        RustUnsafeFuncsMarking(*llvmModule);

        int allbbcount = 0;
        std::set<const BasicBlock*> allBasicBlocks = countAllBB(svfModule, icfg, &allbbcount, 0);
        
        // LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
        // Module *llvmModule = ModSet->getMainLLVMModule(); 

        allbbcount = allBBcount(*llvmModule);
        
        setTogglePointsAsyncSafe(*llvmModule);
        S2FuzzStaticAnalysis_async_safe(svfModule, llvmModule, svfg, icfg, callgraph, 0, debug_pruning_rate, allbbcount, allBasicBlocks);
    }

    if(rustgo_no_safecode_feedback) {
        LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
        Module *llvmModule = ModSet->getMainLLVMModule(); 
        UnsafePreprocess(llvmModule);
    }

    if(afl_manual_target_collection) {

        std::string funcName = TargetFunction;
        std::string bbName = TargetBB;
        LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
        Module *llvmModule = ModSet->getMainLLVMModule(); 
        S2FuzzManualTargetCollection(svfModule, llvmModule, svfg, icfg, callgraph, funcName, bbName);
    }

    if(afl_manual_analysis_input_sharing) {
        RustStdLibrariesMarking(*llvmModule, svfModule, callgraph);
        RustUnsafeFuncsMarking(*llvmModule);
        std::vector<int> nodeIDs(TargetNodes.begin(), TargetNodes.end());
        int allbbcount = 0;
        std::set<const BasicBlock*> allBasicBlocks = countAllBB(svfModule, icfg, &allbbcount, 0);

        // LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
        // Module *llvmModule = ModSet->getMainLLVMModule(); 

        allbbcount = allBBcount(*llvmModule);
        std::cout << "[WHOLE MODULE] ALL BB count : [" << allbbcount << "]" << "\n";
        
        setTogglePointsInputSharingMode(*llvmModule);
        S2FuzzManualTargetAnalysis(svfModule, llvmModule, svfg, icfg, callgraph, allbbcount, allBasicBlocks, nodeIDs);
    }


    if(afl_manual_analysis_no_input_sharing) {
        RustStdLibrariesMarking(*llvmModule, svfModule, callgraph);
        RustUnsafeFuncsMarking(*llvmModule);
        std::vector<int> nodeIDs(TargetNodes.begin(), TargetNodes.end());
        int allbbcount = 0;
        std::set<const BasicBlock*> allBasicBlocks = countAllBB(svfModule, icfg, &allbbcount, 0);

        // LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
        // Module *llvmModule = ModSet->getMainLLVMModule(); 

        allbbcount = allBBcount(*llvmModule);
        std::cout << "[WHOLE MODULE] ALL BB count : [" << allbbcount << "]" << "\n";
        
        setTogglePoints(*llvmModule);
        S2FuzzManualTargetAnalysis(svfModule, llvmModule, svfg, icfg, callgraph, allbbcount, allBasicBlocks, nodeIDs);
    }


    if(afl_manual_analysis_async_safe) {
        RustStdLibrariesMarking(*llvmModule, svfModule, callgraph);
        RustAsyncFuncsMarking(*llvmModule);
        RustUnsafeFuncsMarking(*llvmModule);
        
        std::vector<int> nodeIDs(TargetNodes.begin(), TargetNodes.end());
        int allbbcount = 0;
        std::set<const BasicBlock*> allBasicBlocks = countAllBBAsyncSafe(svfModule, icfg, &allbbcount, 0);

        // LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
        // Module *llvmModule = ModSet->getMainLLVMModule(); 

        allbbcount = allBBcount(*llvmModule);
        std::cout << "[WHOLE MODULE] ALL BB count : [" << allbbcount << "]" << "\n";
        
        setTogglePointsAsyncSafe(*llvmModule);
        S2FuzzManualTargetAnalysis(svfModule, llvmModule, svfg, icfg, callgraph, allbbcount, allBasicBlocks, nodeIDs);
    }


    if(afl_forward_analysis) {
        analyzeICFGWithPaths(svfModule, icfg, "4main17", "foo");
    }

    if(afl_opt) {

        bool debug_pruning_rate = false;

        if(pruning_rate_information) {
            debug_pruning_rate = true;
        }

        if(static_time_evaluation) {
            debug_pruning_rate = false;
        }

        std::string thread = TargetFunction;
        int thread_count = stoi(thread);

        int allbbcount = 0;
        std::set<const BasicBlock*> allBasicBlocks = countAllBB(svfModule, icfg, &allbbcount, 0);
        
        // LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
        // Module *llvmModule = ModSet->getMainLLVMModule(); 

        allbbcount = allBBcount(*llvmModule);
        RustStdLibrariesMarking(*llvmModule, svfModule, callgraph);
        RustUnsafeFuncsMarking(*llvmModule);
        setTogglePoints(*llvmModule);
        ParallelS2FuzzStaticAnalysis(svfModule, llvmModule, svfg, icfg, callgraph, 0, debug_pruning_rate, allbbcount, allBasicBlocks, thread_count);
    }

    // if(mode_base) {
    //     int main_reachable_bb_count = 0;
    //     std::set<const BasicBlock*> allBasicBlocks = countAllBB(svfModule, icfg, &main_reachable_bb_count, 0);
    //     LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
    //     Module *llvmModule = ModSet->getMainLLVMModule(); 
    //     int allbbcount = 0;
    //     allbbcount = allBBcount(*llvmModule);
    //     RustStdLibrariesMarking(*llvmModule, svfModule, callgraph);
    //     RustGoBaseModeAnalysis(svfModule, llvmModule, svfg, icfg, callgraph, main_reachable_bb_count, allBasicBlocks);
    // }

    // if(mode_1) {
    //     int main_reachable_bb_count = 0;
    //     std::set<const BasicBlock*> allBasicBlocks = countAllBB(svfModule, icfg, &main_reachable_bb_count, 0);
    //     LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
    //     Module *llvmModule = ModSet->getMainLLVMModule(); 
    //     int allbbcount = 0;
    //     allbbcount = allBBcount(*llvmModule);
    //     RustStdLibrariesMarking(*llvmModule, svfModule, callgraph);       
    //     // setTogglePoints(*llvmModule); 
    //     RustGoMode1Analysis(svfModule, llvmModule, svfg, icfg, callgraph, main_reachable_bb_count, allBasicBlocks);
    // }


    // if(mode_basic_pruning) {
    //     int allbbcount = 0;
    //     std::set<const BasicBlock*> allBasicBlocks = countAllBB(svfModule, icfg, &allbbcount, 0);
    //     LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
    //     Module *llvmModule = ModSet->getMainLLVMModule(); 
    //     allbbcount = allBBcount(*llvmModule);
    //     RustStdLibrariesMarking(*llvmModule, svfModule, callgraph);       
    //     setTogglePoints(*llvmModule); 
    //     RustGoBasicPruningAnalysis(svfModule, llvmModule, svfg, icfg, callgraph, allbbcount, allBasicBlocks);
    // }

    if(ablation_module_base) {
        RustStdLibrariesMarking(*llvmModule, svfModule, callgraph);
        RustUnsafeFuncsMarking(*llvmModule);
        std::vector<int> nodeIDs(TargetNodes.begin(), TargetNodes.end());
        int allbbcount = 0;
        std::set<const BasicBlock*> allBasicBlocks = countAllBB(svfModule, icfg, &allbbcount, 0);

        // LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
        // Module *llvmModule = ModSet->getMainLLVMModule(); 

        allbbcount = allBBcount(*llvmModule);
        std::cout << "[WHOLE MODULE] ALL BB count : [" << allbbcount << "]" << "\n";

        // setTogglePoints(*llvmModule);
        AblationStudyModuleBase(svfModule, llvmModule, svfg, icfg, callgraph, allbbcount, allBasicBlocks, nodeIDs);
    }

    if(disable_std_lib_opt) {
        RustStdLibrariesMarking(*llvmModule, svfModule, callgraph);
        RustUnsafeFuncsMarking(*llvmModule);
        std::vector<int> nodeIDs(TargetNodes.begin(), TargetNodes.end());
        int allbbcount = 0;
        std::set<const BasicBlock*> allBasicBlocks = countAllBB(svfModule, icfg, &allbbcount, 0);

        // LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
        // Module *llvmModule = ModSet->getMainLLVMModule(); 

        allbbcount = allBBcount(*llvmModule);
        std::cout << "[WHOLE MODULE] ALL BB count : [" << allbbcount << "]" << "\n";
        setTogglePoints(*llvmModule);
        DisableStdLibOpt(svfModule, llvmModule, svfg, icfg, callgraph, allbbcount, allBasicBlocks, nodeIDs);    
    }

    if(newvul_ablation_module_base) {

        bool debug_pruning_rate = false;

        if(pruning_rate_information) {
            debug_pruning_rate = true;
        }

        if(static_time_evaluation) {
            debug_pruning_rate = false;
        }

        RustStdLibrariesMarking(*llvmModule, svfModule, callgraph);
        RustUnsafeFuncsMarking(*llvmModule);

        int allbbcount = 0;
        std::set<const BasicBlock*> allBasicBlocks = countAllBB(svfModule, icfg, &allbbcount, 0);
        
        // LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
        // Module *llvmModule = ModSet->getMainLLVMModule(); 

        allbbcount = allBBcount(*llvmModule);
        
        // setTogglePoints(*llvmModule);
        NewVulAblationStudyModuleBase(svfModule, llvmModule, svfg, icfg, callgraph, 0, debug_pruning_rate, allbbcount, allBasicBlocks);
    }

    // if(ablation_module_1) {

    //     int allbbcount = 0;
    //     std::set<const BasicBlock*> allBasicBlocks = countAllBB(svfModule, icfg, &allbbcount, 0);
        
    //     LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
    //     Module *llvmModule = ModSet->getMainLLVMModule(); 

    //     allbbcount = allBBcount(*llvmModule);
    //     RustStdLibrariesMarking(*llvmModule, svfModule, callgraph);
    //     setTogglePoints(*llvmModule);
    //     AblationModule1(svfModule, llvmModule, svfg, icfg, callgraph, allbbcount, allBasicBlocks);
    // }


    // if(ablation_module_2) {

    //     int allbbcount = 0;
    //     std::set<const BasicBlock*> allBasicBlocks = countAllBB(svfModule, icfg, &allbbcount, 0);
        
    //     LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
    //     Module *llvmModule = ModSet->getMainLLVMModule(); 

    //     allbbcount = allBBcount(*llvmModule);
    //     RustStdLibrariesMarking(*llvmModule, svfModule, callgraph);
    //     setTogglePoints(*llvmModule);
    //     AblationModule2(svfModule, llvmModule, svfg, icfg, callgraph, allbbcount, allBasicBlocks);
    // }

    // if(libfuzzer) {
    //     LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
    //     Module *llvmModule = ModSet->getMainLLVMModule(); 
    //     RustStdLibrariesMarking(*llvmModule, svfModule, callgraph);
    //     S2FuzzStaticAnalysis(svfModule, llvmModule, svfg, icfg, callgraph, 1, 0);
    // }


    if(RustDebugLoc) {
        std::string target = TargetFunction;
        int n = stoi(target);
        std::set<const ICFGNode*> toggle;
        std::set<const SVFGEdge*> visited;
        debugdebug(svfg, n, toggle, visited);

        




        // // All Functions //
        // // const SVFFunction* mainFun = get_main_function(svfModule);
        // // countAllBB(svfModule, icfg);
        // // const Function* main = get_function("_ZN12decode_image1_19__libfuzzer_sys_run17h33f755129cb4ce6dE", svfModule);
        // // const SVFFunction* svfMain = svfModule->getSVFFunction(main);


        // LLVMModuleSet *ModSet = LLVMModuleSet::getLLVMModuleSet();
        // Module *llvmModule = ModSet->getMainLLVMModule();
        // ofstream myfile;
        // myfile.open("debugSTD.txt", ios::out | ios::app);
 
    

        // for(auto &function : *llvmModule) {
        //     if(function.getName().str() == "_ZN4core6result19Result$LT$T$C$E$GT$3map17he0389b46e0c3eb39E") {
        //         const SVFFunction* svfFunc = svfModule->getSVFFunction(&function);
        //         PTACallGraphNode* callgraphNode = callgraph->getCallGraphNode(svfFunc);
        //         std::set<Function*> functions = debugfuncs(callgraphNode, myfile);
        //     }

        // }


        // myfile.close();

    }
        



    delete svfg;
    AndersenWaveDiff::releaseAndersenWaveDiff();
    PAG::releasePAG();



    return 0;

    //#############################################Prev_WORK##########################################################################

    // icfg->dump("icfg");
    // /// Value-Flow Graph (VFG)
    // VFG* vfg = new VFG(callgraph);
    // vfg->dump("vfg");
    // /// Sparse value-flow graph (SVFG)
    // SVFGBuilder svfBuilder;
    // SVFG* svfg = svfBuilder.buildFullSVFGWithoutOPT(ander);


    // // clean up memory
    // delete vfg;
    // delete svfg;
    // AndersenWaveDiff::releaseAndersenWaveDiff();
    // PAG::releasePAG();

    // LLVMModuleSet::getLLVMModuleSet()->dumpModulesToFile(".svf.bc");

}