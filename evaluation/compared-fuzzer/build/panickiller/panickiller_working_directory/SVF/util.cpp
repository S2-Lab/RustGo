#include "ERAsan/util.h"
#include <string>
/*
 * Server implementation helpers
 */


/* ydy - panicKiller stuffs */
#include "sys/shm.h"
#include "llvm/IR/IRBuilder.h"

typedef uint8_t   u8;
// const char UnsafeTargetInstName[] = "__afl_unsafe_hook";
// FunctionCallee UnsafeTargetInstFunc;

static inline bool isUnsafeInst(const Instruction &I) {
  return I.getMetadata("rawptr") ||
         I.getMetadata("rawptr_deref") ||
         I.getMetadata("HeapafterDropAccess") ||
         I.getMetadata("StackafterReturnAccess");
}

static inline bool isUnsafeBB(const BasicBlock &BB) {
  for (auto &I : BB) {
    if (isUnsafeInst(I)) return true;
  }
  return false;
}


void instPanicKillerTargetPoints(Module& M) {
    ofstream myfile;

    myfile.open("PanicKillerTargetInfo.txt", ios::out | ios::trunc);
    LLVMContext &C = M.getContext();

    llvm::Type *BoolTy = Type::getInt1Ty(C);

    llvm::Type *Int8PtrTy = Type::getInt8PtrTy(C);
    llvm::Type *Int8Ty = Type::getInt8Ty(C);
    Type *VoidTy = Type::getVoidTy(C);
    llvm::Type *Int64Ty = Type::getInt64Ty(C);
    llvm::Type *Int32Ty = Type::getInt32Ty(C);
    int unsafe_site_index = 0;

    // UnsafeTargetInstFunc = M.getOrInsertFunction(UnsafeTargetInstName, VoidTy, Int32Ty);

    GlobalVariable *UnsafeTargetMapPtr = 
        new GlobalVariable(M, Int8PtrTy, false,
                            GlobalValue::ExternalLinkage, 0, "__afl_unsafe_ptr");
    llvm::Value* Zero = llvm::ConstantInt::get(Int64Ty, 0);

    for(auto& F: M) {
        if (F.isDeclaration()) continue;
        // std::set<BasicBlock*> visited;

        for(auto& BB: F) {
            if (BB.empty()) continue;
            if (!isUnsafeBB(BB)) continue;

            myfile << "Function : " << F.getName().str() << "\n";
            
            llvm::IRBuilder<> IRB(&*BB.getFirstInsertionPt());

            llvm::LoadInst* unsafePtr = IRB.CreateLoad(Int8PtrTy, UnsafeTargetMapPtr);
            unsafePtr->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));

            llvm::Value *siteId = ConstantInt::get(Int64Ty, unsafe_site_index++);

            llvm::Value* idPtr = IRB.CreateGEP(Int8Ty, unsafePtr, siteId);

            llvm::StoreInst* st = IRB.CreateStore(ConstantInt::get(Int8Ty, 1), idPtr);
            st->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));

        }
    }

    writeModuleToFile(&M, "panicKiller.ll");


}



set<const Function*> RustStdLibraryFuncs;
set<const Function*> RustThreadFuncs;

bool is_main_func(Function& F) {
    if(F.getName().str() == "main") {
        return true;
    }
    return false;
}

std::string get_func_name(std::map<uint32_t, std::set<std::string>>& idxmap, uint32_t idx) {
    if (idxmap[idx].size() > 1) {
        // spdlog::debug("More than one function for Idx:{}", idx); 
    }
    return *(idxmap[idx].begin());
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
const Function* get_function(std::string functionName, SVFModule* svfModule) {
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
    return function;
    // Get corresponding call graph node for the target function
    // return svfModule->getSVFFunction(function);
}


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

ICFGNode* getICFGNode(ICFG* icfg, const Instruction* inst) {
    ICFGNode* iNode = icfg->getBlockICFGNode(inst);

    return iNode;    
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

bool isRustProgEntryFunction(const SVFFunction * fun) {
    return fun && ((fun->getName().str().find("4main17") != std::string::npos) && (fun->getName().str().find("ZN") != std::string::npos)) ;
}



// Find the entry point of a function
const Instruction* get_inst(std::string functionName, SVFModule* svfModule) {
    const Function* function = get_function(functionName, svfModule);
    if (function) {
        const BasicBlock &BB = function->getEntryBlock();
        const Instruction *inst = BB.getTerminator(); 
        return inst;
    } else {
        return NULL;
    }
}



std::string demangleString(const char* name) 
{
  int status = -1; 
  std::unique_ptr<char, void(*)(void*)> res { abi::__cxa_demangle(name, NULL, NULL, &status), std::free };
  return (status == 0) ? res.get() : std::string(name);
}

bool isThreadlisted(const Function *F) {
    if(F->hasFnAttribute("RUST_THREAD_FUNCS")) {
        return true;
    }
    return false;
}


bool isBlacklisted(const Function *F) {
    if(F->hasFnAttribute("RUST_STANDARD_LIB")) {
        return true;
    }
    return false;
}

bool isDropListed(const Function *F) {
    std::set<std::string> DropList = {
       "core::ptr::drop_in_place"
    };
    for (auto const &DropListFunc : DropList) {
        string func_name = demangleString(F->getName().str().c_str());
        
        if (func_name.rfind(DropListFunc, 0) == 0) {
            return true;
        }
        // if (F->getName().startswith(BlackListFunc)) {
        //     return true;
        // }
    }

    return false;

}

void writeModuleToFile(llvm::Module *M, const std::string &file) {
    std::error_code EC;
    llvm::raw_fd_ostream OS(file, EC);

    if (EC) {
        llvm::errs() << "Error opening file: " << EC.message() << "\n";
        return;
    }

    M->print(OS, nullptr); // This will print the module to the given file stream
    
}