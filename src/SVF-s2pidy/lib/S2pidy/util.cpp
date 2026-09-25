#include "S2pidy/util.h"
#include <string>
/*
 * Server implementation helpers
 */

/* ydy - toggleFuzzer stuffs */
#include "sys/shm.h"
#include "llvm/IR/IRBuilder.h"
// #include "debug.h"

#ifndef TOGGLE_FUZZER_H
#define TOGGLE_FUZZER_H


std::set<const BasicBlock*> panicBBs;

typedef uint8_t  u8;
const char ToggleCmpFuncName[] = "__toggle_cmp_func";
FunctionCallee ToggleCmpFunc;

// const char BBCoverageUpdateFuncName[] = "__afl_bb_coverage_update";
// FunctionCallee BBCoverageUpdateFunc;


/* Toggle Index Debugging*/
// const char ToggleMetadataFuncName[] = "__toggle_metadata_func";
// FunctionCallee ToggleMetadataFunc;


void setTogglePoints(Module& M) {
    ofstream myfile;
    
    myfile.open("TotalBBIndexCount.txt", ios::out | ios::trunc);
    LLVMContext &C = M.getContext();

    llvm::Type *BoolTy = Type::getInt1Ty(C);

    llvm::Type *Int8PtrTy = Type::getInt8PtrTy(C);
    llvm::Type *Int8Ty = Type::getInt8Ty(C);
    Type *VoidTy = Type::getVoidTy(C);
    llvm::Type *Int64Ty = Type::getInt64Ty(C);
    llvm::Type *Int32Ty = Type::getInt32Ty(C);

    char* demangle_buff = (char*) malloc(256);
    memset(demangle_buff,0,256);
    int bb_global_index = 0;

    ToggleCmpFunc = M.getOrInsertFunction(ToggleCmpFuncName, VoidTy, Int8Ty);

    /* Toggle Index Debugging*/
    // ToggleMetadataFunc = M.getOrInsertFunction(ToggleMetadataFuncName, VoidTy, Int32Ty);

    GlobalVariable *ToggleMapPtr = 
        new GlobalVariable(M, Int8PtrTy, false,
                            GlobalValue::ExternalLinkage, 0, "__afl_toggle_ptr");
    llvm::Value* Zero = llvm::ConstantInt::get(Int64Ty, 0);
    

    for(auto& F: M) {
        if(F.isDeclaration() || isBlacklisted(&F) || is_main_func(F)) continue;
        std::set<BasicBlock*> visited;

        for(auto& BB: F){
            std::string BBName = BB.getName().str();
            if(BBName.find("panic") != std::string::npos || BBName.find("cleanup") != std::string::npos || BBName.find("unreachable") != std::string::npos 
                    || BBName.find("abort") != std::string::npos)
            {
                traceConnectedBlocks(&BB, visited);
            }

        }

        for(auto& BB: F) {
            if(BB.empty() || (visited.find(&BB) != visited.end())) continue;

            BasicBlock::iterator IP = BB.getFirstInsertionPt();
            llvm::IRBuilder<> IRB(&(*IP));            
            llvm::LoadInst* togglePtr = IRB.CreateLoad(Int8PtrTy, ToggleMapPtr);
            togglePtr->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));

            llvm::Value* index = llvm::ConstantInt::get(Int64Ty, bb_global_index);

            /* Toggle Index Debugging*/
            // llvm::Value* index_32 = llvm::ConstantInt::get(Int32Ty, bb_global_index);

            llvm::Value* bbPtr = IRB.CreateGEP(Int8Ty, togglePtr, index);

            llvm::LoadInst* toggleVal = IRB.CreateLoad(Int8Ty, bbPtr);
            toggleVal->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));

            llvm::CallInst *toggleFunc = IRB.CreateCall(ToggleCmpFunc, toggleVal);
            toggleFunc->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));

            /* Toggle Index Debugging*/
            // llvm::CallInst *toggleMetaFunc = IRB.CreateCall(ToggleMetadataFunc, index_32);
            // toggleMetaFunc->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));

            llvm::ConstantInt *metadataValue = ConstantInt::get(Type::getInt64Ty(C), bb_global_index);
            llvm::MDTuple *metadataNode = MDNode::get(C, ConstantAsMetadata::get(metadataValue));
            toggleFunc->setMetadata("bb_index", metadataNode);

            bb_global_index++;
        }


    }

    myfile << bb_global_index << "\n";

    myfile.close();
    // writeModuleToFile(&M, "toggle.ll");

}

void setTogglePointsAsyncSafe(Module& M) {
    ofstream myfile;
    
    myfile.open("TotalBBIndexCount.txt", ios::out | ios::trunc);
    LLVMContext &C = M.getContext();

    llvm::Type *BoolTy = Type::getInt1Ty(C);

    llvm::Type *Int8PtrTy = Type::getInt8PtrTy(C);
    llvm::Type *Int8Ty = Type::getInt8Ty(C);
    Type *VoidTy = Type::getVoidTy(C);
    llvm::Type *Int64Ty = Type::getInt64Ty(C);
    llvm::Type *Int32Ty = Type::getInt32Ty(C);

    char* demangle_buff = (char*) malloc(256);
    memset(demangle_buff,0,256);
    int bb_global_index = 0;

    ToggleCmpFunc = M.getOrInsertFunction(ToggleCmpFuncName, VoidTy, Int8Ty);

    /* Toggle Index Debugging*/
    // ToggleMetadataFunc = M.getOrInsertFunction(ToggleMetadataFuncName, VoidTy, Int32Ty);

    GlobalVariable *ToggleMapPtr = 
        new GlobalVariable(M, Int8PtrTy, false,
                            GlobalValue::ExternalLinkage, 0, "__afl_toggle_ptr");
    llvm::Value* Zero = llvm::ConstantInt::get(Int64Ty, 0);
    

    for(auto& F: M) {
        if(F.isDeclaration() || isBlacklisted(&F) || is_main_func(F) || isAsynclisted(&F)) continue;
        std::set<BasicBlock*> visited;

        for(auto& BB: F){
            std::string BBName = BB.getName().str();
            if(BBName.find("panic") != std::string::npos || BBName.find("cleanup") != std::string::npos || BBName.find("unreachable") != std::string::npos 
                    || BBName.find("abort") != std::string::npos)
            {
                traceConnectedBlocks(&BB, visited);
            }

        }

        for(auto& BB: F) {
            if(BB.empty() || (visited.find(&BB) != visited.end())) continue;

            BasicBlock::iterator IP = BB.getFirstInsertionPt();
            llvm::IRBuilder<> IRB(&(*IP));            
            llvm::LoadInst* togglePtr = IRB.CreateLoad(Int8PtrTy, ToggleMapPtr);
            togglePtr->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));

            llvm::Value* index = llvm::ConstantInt::get(Int64Ty, bb_global_index);

            /* Toggle Index Debugging*/
            // llvm::Value* index_32 = llvm::ConstantInt::get(Int32Ty, bb_global_index);

            llvm::Value* bbPtr = IRB.CreateGEP(Int8Ty, togglePtr, index);

            llvm::LoadInst* toggleVal = IRB.CreateLoad(Int8Ty, bbPtr);
            toggleVal->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));

            llvm::CallInst *toggleFunc = IRB.CreateCall(ToggleCmpFunc, toggleVal);
            toggleFunc->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));

            /* Toggle Index Debugging*/
            // llvm::CallInst *toggleMetaFunc = IRB.CreateCall(ToggleMetadataFunc, index_32);
            // toggleMetaFunc->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));

            llvm::ConstantInt *metadataValue = ConstantInt::get(Type::getInt64Ty(C), bb_global_index);
            llvm::MDTuple *metadataNode = MDNode::get(C, ConstantAsMetadata::get(metadataValue));
            toggleFunc->setMetadata("bb_index", metadataNode);

            bb_global_index++;
        }


    }

    myfile << bb_global_index << "\n";

    myfile.close();
    // writeModuleToFile(&M, "toggle.ll");

}


void setTogglePointsInputSharingMode(Module& M) {
    ofstream myfile;
    
    myfile.open("TotalBBIndexCount.txt", ios::out | ios::trunc);
    LLVMContext &C = M.getContext();

    llvm::Type *BoolTy = Type::getInt1Ty(C);

    llvm::Type *Int8PtrTy = Type::getInt8PtrTy(C);
    llvm::Type *Int8Ty = Type::getInt8Ty(C);
    Type *VoidTy = Type::getVoidTy(C);
    llvm::Type *Int64Ty = Type::getInt64Ty(C);
    llvm::Type *Int32Ty = Type::getInt32Ty(C);

    char* demangle_buff = (char*) malloc(256);
    memset(demangle_buff,0,256);
    int bb_global_index = 0;

    ToggleCmpFunc = M.getOrInsertFunction(ToggleCmpFuncName, VoidTy, Int8Ty);

    /* Toggle Index Debugging*/
    // ToggleMetadataFunc = M.getOrInsertFunction(ToggleMetadataFuncName, VoidTy, Int32Ty);

    GlobalVariable *ToggleMapPtr = 
        new GlobalVariable(M, Int8PtrTy, false,
                            GlobalValue::ExternalLinkage, 0, "__afl_toggle_ptr");
    GlobalVariable *BBCoverageMapPtr = 
        new GlobalVariable(M, Int8PtrTy, false,
                            GlobalValue::ExternalLinkage, 0, "__afl_bb_coverage_ptr");
    llvm::Value* Zero = llvm::ConstantInt::get(Int64Ty, 0);
    llvm::Value* One = llvm::ConstantInt::get(Int8Ty, 1);
    

    for(auto& F: M) {
        if(F.isDeclaration() || isBlacklisted(&F) || is_main_func(F)) continue;
        std::set<BasicBlock*> visited;

        for(auto& BB: F){
            std::string BBName = BB.getName().str();
            if(BBName.find("panic") != std::string::npos || BBName.find("cleanup") != std::string::npos || BBName.find("unreachable") != std::string::npos 
                    || BBName.find("abort") != std::string::npos)
            {
                traceConnectedBlocks(&BB, visited);
            }

        }

        for(auto& BB: F) {
            if(BB.empty() || (visited.find(&BB) != visited.end())) continue;

            BasicBlock::iterator IP = BB.getFirstInsertionPt();
            llvm::IRBuilder<> IRB(&(*IP));            

            llvm::ConstantInt *metadataValue = ConstantInt::get(Type::getInt64Ty(C), bb_global_index);
            llvm::MDTuple *metadataNode = MDNode::get(C, ConstantAsMetadata::get(metadataValue));
            

            llvm::LoadInst* togglePtr = IRB.CreateLoad(Int8PtrTy, ToggleMapPtr);
            togglePtr->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
            llvm::Value* index = llvm::ConstantInt::get(Int64Ty, bb_global_index);

            llvm::LoadInst *bbMapPtr = IRB.CreateLoad(Int8PtrTy, BBCoverageMapPtr);
            bbMapPtr->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));

            llvm::Value* bbMapIndex = IRB.CreateGEP(Int8Ty, bbMapPtr, index);
            llvm::StoreInst *bbMapStore = IRB.CreateStore(One, bbMapIndex);
            bbMapStore->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));



            llvm::Value* bbPtr = IRB.CreateGEP(Int8Ty, togglePtr, index);

            llvm::LoadInst* toggleVal = IRB.CreateLoad(Int8Ty, bbPtr);
            toggleVal->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));

            llvm::CallInst *toggleFunc = IRB.CreateCall(ToggleCmpFunc, toggleVal);
            toggleFunc->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));

            toggleFunc->setMetadata("bb_index", metadataNode);


            /* Toggle Index Debugging*/
            // llvm::Value* index_32 = llvm::ConstantInt::get(Int32Ty, bb_global_index);

            /* Toggle Index Debugging*/
            // llvm::CallInst *toggleMetaFunc = IRB.CreateCall(ToggleMetadataFunc, index_32);
            // toggleMetaFunc->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));



            bb_global_index++;
        }


    }

    myfile << bb_global_index << "\n";

    myfile.close();
    // writeModuleToFile(&M, "toggle.ll");

}

const char PruningCmpFuncName[] = "__insert_exit";
FunctionCallee PruningCmpFunc;

void instrumentUnreachableFuncs(Module& M, std::set<std::string> allowlist) {

    int total_func = 0;
    int pruned_func = 0;
    LLVMContext &C = M.getContext();
    Type *VoidTy = Type::getVoidTy(C);
    PruningCmpFunc = M.getOrInsertFunction(PruningCmpFuncName, VoidTy);
    for(auto& F: M) {
        if(F.isDeclaration() || isBlacklisted(&F) || is_main_func(F)) continue;
        if (F.getName().str().find("exit") != std::string::npos) {
            std::cout << F.getName().str() << "\n";
        }
        total_func++;
        std::set<BasicBlock*> visited;
        for(auto& BB: F){
            std::string BBName = BB.getName().str();
            if(BBName.find("panic") != std::string::npos || BBName.find("cleanup") != std::string::npos || BBName.find("unreachable") != std::string::npos 
                    || BBName.find("abort") != std::string::npos)
            {
                traceConnectedBlocks(&BB, visited);
            }
        }

        // if (!F.isDeclaration() && allowlist.find(F.getName().str()) == allowlist.end()) {
        //     BasicBlock &LastBB = F.back();
        //     llvm::IRBuilder<> IRB(LastBB.getTerminator());
        //     llvm::CallInst *toggleFunc = IRB.CreateCall(PruningCmpFunc);
        //     toggleFunc->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
        //     pruned_func++;
        // }

        if(allowlist.find(F.getName().str()) != allowlist.end()) continue;


        BasicBlock &LastBB = F.back();
        llvm::IRBuilder<> IRB(LastBB.getTerminator());
        llvm::CallInst *toggleFunc = IRB.CreateCall(PruningCmpFunc);
        toggleFunc->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
        pruned_func++;

        // for(auto& BB: F) {
            
        //     BasicBlock::iterator IP = BB.getFirstInsertionPt();
        //     llvm::IRBuilder<> IRB(&(*IP));    
        //     llvm::CallInst *toggleFunc = IRB.CreateCall(PruningCmpFunc);
        //     toggleFunc->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));        
        // }
    }
    // std::cout << "\n ******Total functions : " << total_func << "******" << "\n";
    // std::cout << "\n ******Pruned functions : " << pruned_func << "******" << "\n";
    // std::cout << "\n ******Pruning rate : " << ((double)pruned_func / (double)total_func) * 100 << "******" << "\n";
}


#endif 

bool is_main_func(Function& F) {
    if(F.getName().str() == "main") {
        return true;
    }
    return false;
}

bool is_subset(const std::set<uint64_t>& subset, const std::set<uint64_t>& superset) {
    return std::includes(superset.begin(), superset.end(), subset.begin(), subset.end());
}

set<const Function*> RustStdLibraryFuncs;
set<const Function*> RustThreadFuncs;
set<const Function*> RustAsyncFuncs;
set<const Function*> RustUnsafeFuncs;
set<const Function*> RustDropStdLibraryFuncs;
set<const Function*> RustCMLibraries;


void sigchld_handler(int s) {
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0);
    errno = saved_errno;
}

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/* 
 * Misc helpers
 */

// Dumps all the allowlisted function ID's to the given file
void dump_ids(std::string filename, std::set<uint32_t> allowed_ids) {
    ofstream ids_file;
    ids_file.open (filename, ios::out | ios::trunc);
    if (! ids_file.is_open()) {
        // spdlog::critical("could not open indirect file..exiting");
        abort();
    }
    else {
        for (auto it=allowed_ids.begin(); it != allowed_ids.end(); ++it) { 
            ids_file << *it << "\n";
        }
        ids_file.close();
    }
}

// Dumps number of allowlisted functions and the number of indirect edges discovered during dynamic analysis
void dump_num(std::string filename, std::size_t allowlisted, std::size_t allowed_indir) {
    ofstream stats_file;
    stats_file.open (filename, ios::out | ios::app);
    if (! stats_file.is_open()) {
        // spdlog::critical("could not open indirect file..exiting");
        abort();
    }
    else {
        stats_file << "Allowlisted:" << allowlisted << " Indirect:" << allowed_indir << "\n";
        stats_file.close();
    }
}

/*
 * Fencing helpers
 */
int64_t get_edge_id(uint32_t caller, uint32_t callee) {
    int64_t id = (int64_t) caller << 32 | callee;
    // Check to ensure that overflow has not occurred while creating this unique id
    if (id < 0) {
        printf("\nOverflow detected during edge ID creation");
        exit(1);
    }
    return id; 
}

std::string get_func_name(std::map<uint32_t, std::set<std::string>>& idxmap, uint32_t idx) {
    if (idxmap[idx].size() > 1) {
        // spdlog::debug("More than one function for Idx:{}", idx); 
    }
    return *(idxmap[idx].begin());
}

PTACallGraphNode* get_callgraph_node(
        SVFModule* svfModule, 
        PTACallGraph* callgraph, 
        std::string target) {

    const Function *target_function = NULL;
    for (SVFModule::llvm_const_iterator I = svfModule->llvmFunBegin(), E =
                svfModule->llvmFunEnd(); I != E; ++I) {
        if ((target.compare((*I)->getName().str())) == 0) { 
	        target_function = *I;
            break;
        }
    }
    const SVFFunction* target_svf = svfModule->getSVFFunction(target_function);
    PTACallGraphNode* target_node = callgraph->getCallGraphNode(target_svf);
    return target_node;
}

bool isReachable(
        const SVFFunction* srcFn, 
        const SVFFunction *dstFn, 
        PTACallGraph* callgraph, 
        std::set<uint64_t>& allowed_indirect,
        std::set<std::string>& inlined_funcs) {
    PTACallGraphNode* dstNode = callgraph->getCallGraphNode(dstFn);

    std::stack<const PTACallGraphNode*> nodeStack;
    NodeBS visitedNodes;
    nodeStack.push(dstNode);
    visitedNodes.set(dstNode->getId());
    // spdlog::debug("+++"); 
    while (nodeStack.empty() == false)
    {

        PTACallGraphNode* node = const_cast<PTACallGraphNode*>(nodeStack.top()); 
        // spdlog::info("Function:{}", (node->getFunction()->getLLVMFun()->getName().str()));
        nodeStack.pop();
        // spdlog::debug("{}", node->getFunction()->getLLVMFun()->getName().str()); 
        if (node->getFunction() == srcFn)
            return true;

        for (PTACallGraph::CallGraphEdgeConstIter it = node->InEdgeBegin(), eit = node->InEdgeEnd(); it != eit; ++it)
        {
            PTACallGraphEdge* edge = *it;
            // Check if there is at least one indirect edge between two functions and that the function has not been inlined
            // if ((edge->isIndirectCallEdge()) || (!(edge->isIndirectCallEdge()) && !(edge->isDirectCallEdge()))) {
            if (edge->isIndirectCallEdge() && (inlined_funcs.find((edge->getSrcNode()->getFunction()->getLLVMFun()->getName().str())) == inlined_funcs.end())) {
                // If there is an indirect edge, and its not been observed before, ignore this
                if (allowed_indirect.find(edge->edgeFlag) == allowed_indirect.end()) { 
                    continue ;
                }
            }
            if (visitedNodes.test_and_set(edge->getSrcID()))
                nodeStack.push(edge->getSrcNode());
        }
    }

    return false;
}


bool isReachableForRust(const SVFFunction* srcFn, const SVFFunction* dstFn, PTACallGraph* callgraph) {
    // std::cout<<"start CallGraph reachability"<<"\n";
    // std::cout<<"prevFun : ["<<srcFn->getName().str()<<"]\n";
    // std::cout<<"targetFun : ["<<dstFn->getName().str()<<"]\n";
    PTACallGraphNode* dstNode = callgraph->getCallGraphNode(dstFn);
    std::stack<const PTACallGraphNode*> nodeStack;
    NodeBS visitedNodes;
    nodeStack.push(dstNode);
    visitedNodes.set(dstNode->getId());

    while(nodeStack.empty() == false)
    {
        PTACallGraphNode* node = const_cast<PTACallGraphNode*>(nodeStack.top());
        nodeStack.pop();
        if(node->getFunction() == srcFn) {
            return true;
        }

        for (PTACallGraph::CallGraphEdgeConstIter it = node->InEdgeBegin(), eit = node->InEdgeEnd(); it != eit; ++it)
        {
            PTACallGraphEdge* edge = *it;
            if(visitedNodes.test_and_set(edge->getSrcID())) {
                nodeStack.push(edge->getSrcNode());
            }
        }
    }

    return false;
}


// Get SVF function corresponding to function name 
const SVFFunction* get_svf_function(std::string functionName, SVFModule* svfModule) {
    const Function *function = NULL;
    for (SVFModule::llvm_const_iterator I = svfModule->llvmFunBegin(), E =
                svfModule->llvmFunEnd(); I != E; ++I) {
        // cout << demangleString((*I)->getName().str().c_str()) << "\n\n";
        // std::cout << (*I)->getName().str() << "\n\n";
        if ((functionName.compare((*I)->getName().str())) == 0) { 
	        function = *I;
            break;
        }
        // std::string mangled_function = (*I)->getName().str();   
        // if((mangled_function.find(functionName) != std::string::npos) && (mangled_function.find("ZN") != std::string::npos)) {
        //     function = *I;
        //     break;
        // }
        
    }
    // return function;
    // Get corresponding call graph node for the target function
    return svfModule->getSVFFunction(function);
}



// Get SVF function corresponding to function name 
const Function* get_function(SVFModule* svfModule) {
    const Function *function = NULL;
    for (SVFModule::llvm_const_iterator I = svfModule->llvmFunBegin(), E =
                svfModule->llvmFunEnd(); I != E; ++I) {

        if((*I) && (!(*I)->isDeclaration()) && ((*I)->getName().str().find("4main") != std::string::npos)) {
            function = *I;
            std::cout << "\n ****** Main Function : [" << (*I)->getName().str() <<"] ******\n";
            break;
        }

            
    }
    return function;

}
    
    // Get corresponding call graph node for the target function
    // return svfModule->getSVFFunction(function);



const SVFFunction* get_main_function(SVFModule* svfModule) {
    const SVFFunction *function = NULL;
    for (SVFModule::llvm_const_iterator I = svfModule->llvmFunBegin(), E =
                svfModule->llvmFunEnd(); I != E; ++I) {
        if(((*I)->getName().str().find("4main17") != std::string::npos) && ((*I)->getName().str().find("ZN") != std::string::npos)) {
            function = svfModule->getSVFFunction(*I);
            break;
        }
        
    }

    return function;
}

void get_allow_BB(ICFG* icfg, const Instruction* inst) {
    ofstream get_allow_BB;
    get_allow_BB.open("get_allow_BB.txt", ios::out | ios::trunc);
    if(!get_allow_BB.is_open()) {
        cout<<"could not open BB file\n";
        abort();
    }

    ICFGNode* iNode = icfg->getBlockICFGNode(inst);
    cout << "First iNode is : " << iNode->getId() << "\n";
    if(iNode->getAllow()) {
        get_allow_BB << iNode->getId() << "\n";
    }
    FIFOWorkList<const ICFGNode*> worklist;
    Set<const ICFGNode*> visited;
    worklist.push(iNode);

    while(!worklist.empty())
    {
        const ICFGNode* vNode = worklist.pop();
        for (ICFGNode::const_iterator vt = vNode->OutEdgeBegin(), evt =
                    vNode->OutEdgeEnd(); vt != evt; ++vt)
        {
            ICFGEdge* edge = *vt;
            ICFGNode* succNode = edge->getDstNode();
            if (visited.find(succNode) == visited.end())
            {
                visited.insert(succNode);
                worklist.push(succNode);
                if(succNode->getAllow()) {
                    get_allow_BB << succNode->getId() <<"\n";
                }

            }
        }
    }

}

ICFGNode* getICFGNode(ICFG* icfg, const Instruction* inst) {
    ICFGNode* iNode = icfg->getBlockICFGNode(inst);

    return iNode;    
}



void count_llvm_functions(SVFModule* svfModule) {
    ofstream count_llvm_functions;
    uint32_t i = 0;
    count_llvm_functions.open("count_llvm_functions.txt", ios::out | ios::trunc);
    if(!count_llvm_functions.is_open()) {
        cout<<"could not open count_llvm_functions file\n";
        abort();
    }
    else {
        for(SVFModule::llvm_const_iterator I = svfModule->llvmFunBegin(), E =
                svfModule->llvmFunEnd(); I != E; ++I) {
            count_llvm_functions << "["<<i<<"]" << " : " << (*I)->getName().str() <<"\n";
            i++;
        }
        count_llvm_functions << "Total functions count : " << i << "\n\n";
    }
}


void print_llvm_function(SVFModule* svfModule) {
    for (SVFModule::llvm_const_iterator I = svfModule->llvmFunBegin(), E =
                svfModule->llvmFunEnd(); I != E; ++I) {
        std::cout << demangleString((*I)->getName().str().c_str()) << "\n\n";
    }
}

void print_svf_function(SVFModule* svfModule, PTACallGraph* callgraph) {
    for (SVFModule::llvm_const_iterator I = svfModule->llvmFunBegin(), E =
                svfModule->llvmFunEnd(); I != E; ++I) {
        const SVFFunction* fun = svfModule->getSVFFunction(*I);
        std::cout << demangleString((fun)->getName().str().c_str()) << "\n\n";
    }
}

void getmain(SVFModule* svfModule) {
    ofstream myfile ("getmain.txt");
    if(!myfile.is_open()) {
        std::cout << "could not open output file exiting";
        exit(1);
    }

    for (SVFModule::llvm_const_iterator I = svfModule->llvmFunBegin(), E =
                svfModule->llvmFunEnd(); I != E; ++I) {
        if(((*I)->getName().str().find("4main17") != std::string::npos) && ((*I)->getName().str().find("ZN") != std::string::npos)) {
            myfile <<"main functions : " << (*I)->getName().str() << "\n";
        }
        
    }
    myfile.close();
    
}

const Instruction* get_main_inst(SVFModule* svfModule, size_t fuzzing_options) {
    
    // for (SVFModule::llvm_const_iterator I = svfModule->llvmFunBegin(), E =
    //             svfModule->llvmFunEnd(); I != E; ++I) {
    //     if((*I)->getName().str() == "_ZN5test24main17h540dd33566fcc448E") {
    //         const Function* function = (*I);
    //         if (function) {
    //             const BasicBlock &BB = function->getEntryBlock();
    //             if(!BB.empty()) {
    //                 // const Instruction* inst = BB.getFirstNonPHI();
    //                 // for(BasicBlock::const_iterator i = BB.begin(), e =
    //                 //         BB.end(); i != e; ++i)
    //                 //     {
    //                 //         ICFGNode* iNode = icfg->getBlockICFGNode(inst);
    //                 //         NodeID nodeId = icfg->getBlockICFGNode(*i)->getId();
    //                 //         cout << "inst for main Basic Block : " << (*i).get << "\n";
    //                 //     }
    //                 const Instruction *inst = &(BB.front());
    //                 // std::cout << "first instruction is " <<inst->getOpcodeName() << "\n";
    //                 return inst;
    //             }

    //         }
    //         break;
    //     } 
    // }

    /* afl.rs version */
    if(fuzzing_options == 0) {
        for (SVFModule::llvm_const_iterator I = svfModule->llvmFunBegin(), E =
                    svfModule->llvmFunEnd(); I != E; ++I) {
            if((*I) && (!(*I)->isDeclaration()) && ((*I)->getName().str().find("4main17") != std::string::npos)) {
                const Function* function = (*I);
                if(function) {
                    // std::cout << "In function countAllBB [Main Entry] : " << function->getName().str() << "\n";
                    const BasicBlock &BB = function->getEntryBlock();
                    if(!BB.empty()) {
                        const Instruction *inst = &(BB.front());
                        return inst;
                    }
                
                }
            }
            
        }
    }

    /* cargo-fuzz version */
    else if (fuzzing_options == 1) {
        for (SVFModule::llvm_const_iterator I = svfModule->llvmFunBegin(), E =
                    svfModule->llvmFunEnd(); I != E; ++I) {
            if((*I)->getName().str() == "rust_fuzzer_test_input") {
                const Function* function = (*I);
                if(function) {
                    const BasicBlock &BB = function->getEntryBlock();
                    if(!BB.empty()) {
                        const Instruction *inst = &(BB.front());
                        return inst;
                    }
                
                }
            }
            
        }
    }



    return NULL;
}


bool isRustProgEntryFunction(const SVFFunction * fun) {
    return fun && ((fun->getName().str().find("4main17") != std::string::npos) && (fun->getName().str().find("ZN") != std::string::npos)) ;
}

bool isReachableFromRustEntry(PTACallGraphNode *cgnode) {
 std::stack<const PTACallGraphNode*> nodeStack;
    NodeBS visitedNodes;
    nodeStack.push(cgnode);
    visitedNodes.set(cgnode->getId());

    while (nodeStack.empty() == false)
    {
        PTACallGraphNode* node = const_cast<PTACallGraphNode*>(nodeStack.top());
        nodeStack.pop();

        if (isRustProgEntryFunction(node->getFunction()))
            return true;

        for (PTACallGraphNode::const_iterator it = node->InEdgeBegin(), eit = node->InEdgeEnd(); it != eit; ++it)
        {
            PTACallGraphEdge* edge = *it;
            if (visitedNodes.test_and_set(edge->getSrcID()))
                nodeStack.push(edge->getSrcNode());
        }
    }

    return false;
}

void getPtsFromInst(PointerAnalysis *pta, ICFG *icfg, const Instruction *inst) 
{
    ICFGNode* iNode = icfg->getBlockICFGNode(inst);
    NodeID piNode = iNode->getId();
    std::cout << "FROM ICFG NODE - NODE ID[" << piNode << "]\n";
    PAGNode* targetObj = pta->getPAG()->getPAGNode(piNode);
    std::cout <<"FROM PAG NODE - NODE ID["<<targetObj->getId() << "]\n";
    if(targetObj->hasValue()) {
        std::cout << "Instruction Value is : " << targetObj->getValueName() << "\n\n";
    }
}


// Processes all the indirect call edge ID's 
void process_indirect_edgeids(std::string& metadata, std::set<uint64_t>& allowed_indirect) {
    std::stringstream ss(metadata);
    std::string token;
    char delim = ' ';
    // The first token corresponds to the indirect call edge
    // Add it to the list of allowed indirect call edges
    while (std::getline(ss, token, delim)) {
        allowed_indirect.insert(std::stoull(token));
        break;
    }
}

void process_indirect_callees(std::string& metadata, std::set<uint64_t>& allowed_indirect, std::set<std::string>& allowlist,
        const SVFFunction* target_svf, PTACallGraph* callgraph, SVFModule* svfModule) {
    std::stringstream ss(metadata);
    std::string token, callee_str;
    int count = 0;
    char delim = ' ';
    while (std::getline(ss, token, delim)) {
        // The first token corresponds to the indirect call edge
        // Add it to the list of allowed indirect call edges
        if (count == 0) {
	    allowed_indirect.insert(std::stol(token));
            count += 1;
        }
        // The second token corresponds to the caller string. We don't do anything with it as of now
        else if (count == 1) {
            count += 1;
        }
        // The second token corresponds to the callee string
        else {
            callee_str = token;
            count = 0;
        }
    }

    // Get the PTACallgraph node corresponding to the callee 
    PTACallGraphNode* callee_node = get_callgraph_node(svfModule, callgraph, callee_str);

    // Insert the callee of the indirect call into the allowlist 
    allowlist.insert(callee_node->getFunction()->getLLVMFun()->getName().str());

    // Insert all the descendants of the callee into the allowlist
    // getForwardDescendants(callee_node, target_svf, allowed_indirect, allowlist, callgraph); 
}


/* Debug Helpers */
void check_edge(SVFModule* svfModule, PTACallGraph* callgraph, std::string caller, std::string callee) { 
    PTACallGraphNode* caller_node = get_callgraph_node(svfModule, callgraph, caller);
    PTACallGraphNode* callee_node = get_callgraph_node(svfModule, callgraph, callee); 
    PTACallGraphEdge *calledge = NULL;
    for (PTACallGraphEdge::CallGraphEdgeSet::iterator it = caller_node->OutEdgeBegin();
           it != caller_node->OutEdgeEnd(); ++it) {
       PTACallGraphEdge* tempEdge = (*it); 
       PTACallGraphNode* dstNode = tempEdge->getDstNode();
       if (dstNode->getId() == callee_node->getId()) {
           calledge = tempEdge; 
        //    spdlog::debug("Found corresponding callgraph edge");
           break;
       }
    }
    if (! calledge) { 
        // spdlog::info("No edge exists");
    }
    else {
        if (calledge->isIndirectCallEdge()) {
            // spdlog::info("Indirect edge exists");
        } else {
            // spdlog::info("Direct edge exists");
        }
    }
}


std::string demangleString(const char* name) 
{
  int status = -1; 
  std::unique_ptr<char, void(*)(void*)> res { abi::__cxa_demangle(name, NULL, NULL, &status), std::free };
  return (status == 0) ? res.get() : std::string(name);
}

// bool isBlacklisted(const Function *F) {
//     std::set<std::string> BlackList = {
//         "asan.",
//         "llvm.",
//         "sancov.",
//         "__ubsan_handle_",
//         "free",
//         "malloc",
//         "calloc",
//         "realloc",
//         /* Rust library*/
//         "alloc::",
//         "backtrace::",
//         "core::",
//         "panic_abort::",
//         "panic_unwind::",
//         "portable-simd::",
//         "proc_macro::",
//         "profiler_builtins::",
//         "rtstartup::",
//         "rustc-std-workspace",
//         "std::",
//         "stdarch::",
//         "unwind::",
//         /* Rust library*/

//         "_$LT$alloc..",
//         "_$LT$backtrace..",
//         "_$LT$core..",
//         "_$LT$panic_abort..",
//         "_$LT$panic_unwind..",
//         "_$LT$portable-simd..",
//         "_$LT$proc_macro..",
//         "_$LT$profiler_builtins..",
//         "_$LT$rtstartup..",
//         "_$LT$rustc-std-workspace",
//         "_$LT$std..",
//         "_$LT$stdarch..",
//         "_$LT$unwind..",

//         "_$LT$$LT$alloc..",
//         "_$LT$$LT$backtrace..",
//         "_$LT$$LT$core..",
//         "_$LT$$LT$panic_abort..",
//         "_$LT$$LT$panic_unwind..",
//         "_$LT$$LT$portable-simd..",
//         "_$LT$$LT$proc_macro..",
//         "_$LT$$LT$profiler_builtins..",
//         "_$LT$$LT$rtstartup..",
//         "_$LT$$LT$rustc-std-workspace",
//         "_$LT$$LT$std..",
//         "_$LT$$LT$stdarch..",
//         "_$LT$$LT$unwind..",

//         "_$LT$I$u20$as$u20$alloc..",
//         "_$LT$I$u20$as$u20$backtrace..",
//         "_$LT$I$u20$as$u20$core..",
//         "_$LT$I$u20$as$u20$panic_abort..",
//         "_$LT$I$u20$as$u20$panic_unwind..",
//         "_$LT$I$u20$as$u20$portable-simd..",
//         "_$LT$I$u20$as$u20$proc_macro..",
//         "_$LT$I$u20$as$u20$profiler_builtins..",
//         "_$LT$I$u20$as$u20$rtstartup..",
//         "_$LT$I$u20$as$u20$rustc-std-workspace",
//         "_$LT$I$u20$as$u20$std..",
//         "_$LT$I$u20$as$u20$stdarch..",
//         "_$LT$I$u20$as$u20$unwind..",


//         "__rust",


//         "_$LT$T$u20$as$u20$alloc..",
//         "_$LT$T$u20$as$u20$backtrace..",
//         "_$LT$T$u20$as$u20$core..",
//         "_$LT$T$u20$as$u20$panic_abort..",
//         "_$LT$T$u20$as$u20$panic_unwind..",
//         "_$LT$T$u20$as$u20$portable-simd..",
//         "_$LT$T$u20$as$u20$proc_macro..",
//         "_$LT$T$u20$as$u20$profiler_builtins..",
//         "_$LT$T$u20$as$u20$rtstartup..",
//         "_$LT$T$u20$as$u20$rustc-std-workspace..",
//         "_$LT$T$u20$as$u20$std..",
//         "_$LT$T$u20$as$u20$stdarch..",
//         "_$LT$T$u20$as$u20$unwind..",
        
//         /* Rust Termination library*/
//         "_$LT$$LP$$RP$$u20$as$u20$std..",

//         /* Rust Personality */
//         "rust_eh_personality"

//         /* Debugging*/
//         // "exchange_malloc"
//     };
//     for (auto const &BlackListFunc : BlackList) {
//         string func_name = demangleString(F->getName().str().c_str());
        
//         if (func_name.rfind(BlackListFunc, 0) == 0) {
//             return true;
//         }
//     }

//     return false;

// }

bool isBlacklisted(const Function *F) {
    if(F->hasFnAttribute("RUST_STANDARD_LIB")) {
        return true;
    }
    return false;
}

//recheck
bool isDroplisted(const Function *F) {
    if(F->hasFnAttribute("RUST_STANDARD_DROP_LIB")) {
        return true;
    }
    return false;
}

bool isCMLibrarylisted(const Function *F) {
    if(F->hasFnAttribute("RUST_CUSTOM_STANDARD_LIB")) {
        return true;
    }
    return false;
}

bool isThreadlisted(const Function *F) {
    if(F->hasFnAttribute("RUST_THREAD_FUNCS")) {
        return true;
    }
    return false;
}

bool isAsynclisted(const Function *F) {
    if(F->hasFnAttribute("RUST_ASYNC_FUNCS")) {
        return true;
    }
    return false;
}

bool isUnsafelisted(const Function *F) {
    if(F->hasFnAttribute("RUST_UNSAFE_FUNCS")) {
        return true;
    }
    return false;
}

void traceConnectedBlocks(BasicBlock* startBB, std::set<BasicBlock*>& visited) {
    std::stack<BasicBlock*> worklist;
    worklist.push(startBB);

    // panicBBs.insert(startBB);

    while (!worklist.empty()) {
        BasicBlock* BB = worklist.top();
        worklist.pop();

        if (visited.find(BB) != visited.end())
            continue;

        visited.insert(BB);
        panicBBs.insert(BB);

        for (succ_iterator SI = succ_begin(BB), SE = succ_end(BB); SI != SE; ++SI) {
            BasicBlock* Succ = *SI;
            if (visited.find(Succ) == visited.end()) {
                worklist.push(Succ);
            }
        }
    }
}




// bool isDropListed(const Function *F) {
//     std::set<std::string> DropList = {
//        "core::ptr::drop_in_place"
//     };
//     for (auto const &DropListFunc : DropList) {
//         string func_name = demangleString(F->getName().str().c_str());
        
//         if (func_name.rfind(DropListFunc, 0) == 0) {
//             return true;
//         }
//         // if (F->getName().startswith(BlackListFunc)) {
//         //     return true;
//         // }
//     }

//     return false;

// }

void writeModuleToFile(llvm::Module *M, const std::string &file) {
    std::error_code EC;
    llvm::raw_fd_ostream OS(file, EC);

    if (EC) {
        llvm::errs() << "Error opening file: " << EC.message() << "\n";
        return;
    }

    M->print(OS, nullptr); // This will print the module to the given file stream
    
}



void read_and_sort_file(const std::string& input_path, const std::string& output_path) {
    std::ifstream inputFile(input_path);
    if (!inputFile) {
        std::cerr << "ToggleInfo.txt not found\n";
        exit(1);
    }
    
    std::vector<std::string> sorted_lines;
    std::string line;
    
    while (std::getline(inputFile, line)) {
        std::istringstream iss(line);
        std::string index, values;
        std::getline(iss, index, ':');
        std::getline(iss, values);

        std::istringstream valueStream(values);
        std::vector<int> nums;
        int num;
        while (valueStream >> num) {
            nums.push_back(num);
        }
        
        std::sort(nums.begin(), nums.end());
        std::ostringstream oss;
        oss << index << " : ";
        for (int val : nums) {
            oss << val << " ";
        }
        sorted_lines.push_back(oss.str());
    }
    inputFile.close();
    
    std::ofstream outputFile(output_path);
    for (const std::string& sorted_line : sorted_lines) {
        outputFile << sorted_line << "\n";
    }
    outputFile.close();
}

void condense_file(const std::string& input_path, const std::string& output_path) {
    std::ifstream inputFile(input_path);
    if (!inputFile) {
        std::cerr << "Input file not found\n";
        exit(1);
    }
    
    std::map<std::string, std::vector<std::string>> unique_lines;
    std::string line;

    while (std::getline(inputFile, line)) {
        std::istringstream iss(line);
        std::string key, values;
        std::getline(iss, key, ':');
        std::getline(iss, values);
        
        values = values.substr(1);  
        unique_lines[values].push_back(key);
    }
    inputFile.close();
    
    std::ofstream outputFile(output_path);
    int new_index = 0;
    for (const auto& entry : unique_lines) {
        outputFile << "[" << new_index++ << "] : " << entry.first << "\n";
    }
    outputFile.close();
}

void remove_subsets(const std::string& input_path, const std::string& output_path) {
    std::ifstream inputFile(input_path);
    if (!inputFile) {
        std::cerr << "Input file not found\n";
        exit(1);
    }
    
    std::vector<std::pair<std::string, std::set<int>>> sets;
    std::string line;

    while (std::getline(inputFile, line)) {
        std::istringstream iss(line);
        std::string index, values;
        std::getline(iss, index, ':');
        std::getline(iss, values);

        std::set<int> s;
        std::istringstream valueStream(values);
        int num;
        while (valueStream >> num) {
            s.insert(num);
        }
        
        sets.push_back({index, s});
    }
    inputFile.close();
    
    std::sort(sets.begin(), sets.end(), [](const auto& a, const auto& b) {
        return a.second.size() > b.second.size();
    });
    
    std::vector<std::set<int>> unique_sets;
    
    for (size_t i = 0; i < sets.size(); ++i) {
        bool is_subset = false;
        for (size_t j = 0; j < sets.size(); ++j) {
            if (i != j && std::includes(sets[j].second.begin(), sets[j].second.end(), sets[i].second.begin(), sets[i].second.end())) {
                is_subset = true;
                break;
            }
        }
        if (!is_subset) {
            unique_sets.push_back(sets[i].second);
        }
    }
    
    std::ofstream outputFile(output_path);
    int new_index = 0;
    for (const auto& s : unique_sets) {
        outputFile << "[" << new_index++ << "] : ";
        for (int val : s) {
            outputFile << val << " ";
        }
        outputFile << "\n";
    }
    outputFile.close();
}

std::set<int> union_sets(const std::vector<std::set<int>>& sets) {
    std::set<int> result;
    for (const auto& s : sets) {
        result.insert(s.begin(), s.end());
    }
    return result;
}

std::set<int> union_sets_from_file(const std::string& file_path) {
    std::ifstream finalFile(file_path);
    std::vector<std::set<int>> sets;
    std::string line;

    while (std::getline(finalFile, line)) {
        std::istringstream iss(line);
        std::string index, values;
        std::getline(iss, index, ':');
        std::getline(iss, values);

        std::set<int> s;
        std::istringstream valueStream(values);
        int num;
        while (valueStream >> num) {
            s.insert(num);
        }
        
        sets.push_back(s);
    }
    finalFile.close();
    
    return union_sets(sets);
}

void write_union_result(const std::string& output_path, const std::set<int>& union_set) {
    std::ofstream outputFile(output_path);
    outputFile << "[0] : ";
    for (int val : union_set) {
        outputFile << val << " ";
    }
    outputFile << "\n";
    outputFile.close();
}




double jaccardSimilarity(const std::set<uint64_t>& set1, const std::set<uint64_t>& set2) {
    size_t intersection_size = 0;
    for (const auto& elem : set1) {
        if (set2.count(elem)) intersection_size++;
    }
    size_t union_size = set1.size() + set2.size() - intersection_size;
    return union_size == 0 ? 0.0 : static_cast<double>(intersection_size) / union_size;
}

size_t blockCountDifference(const std::set<uint64_t>& set1, const std::set<uint64_t>& set2) {
    return std::abs(static_cast<int>(set1.size()) - static_cast<int>(set2.size()));
}

void mergeTargets(std::map<int, ToggleData>& targets, double jaccardThreshold, size_t sizeDiffThreshold) {
    bool merged = false;
    do {
        merged = false;
        std::vector<std::pair<int, int>> mergeCandidates;

        for (auto it1 = targets.begin(); it1 != targets.end(); ++it1) {
            for (auto it2 = std::next(it1); it2 != targets.end(); ++it2) {
                double similarity = jaccardSimilarity(it1->second.BBIds, it2->second.BBIds);
                size_t sizeDiff = blockCountDifference(it1->second.BBIds, it2->second.BBIds);

                if (similarity >= jaccardThreshold && sizeDiff <= sizeDiffThreshold) {
                    mergeCandidates.emplace_back(it1->first, it2->first);
                }
            }
        }

        if (!mergeCandidates.empty()) {
            for (const auto& candidate : mergeCandidates) {
                int idx1 = candidate.first;
                int idx2 = candidate.second;

                targets[idx1].BBIds.insert(targets[idx2].BBIds.begin(), targets[idx2].BBIds.end());

                if (!targets[idx1].lineInfo.empty() && !targets[idx2].lineInfo.empty()) {
                    targets[idx1].lineInfo += "; " + targets[idx2].lineInfo;
                } else if (!targets[idx2].lineInfo.empty()) {
                    targets[idx1].lineInfo = targets[idx2].lineInfo;
                }
                targets.erase(idx2);
                merged = true;
                break; 
            }
        }
    } while (merged);
}