#include "SVF-FE/LLVMUtil.h"
#include "Graphs/SVFG.h"
#include "Graphs/PTACallGraph.h"
#include "WPA/Andersen.h"
#include "SVF-FE/PAGBuilder.h"
#include <iostream>
#include <string>
#include <fstream>
#include <cxxabi.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <chrono>
#include "llvm/Demangle/Demangle.h"
#include <llvm/ADT/StringRef.h>
#include <regex>
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/LLVMContext.h"
#include "RustDemangle.h"
#include <queue>
#include <vector>
#include <dirent.h>


#define BACKLOG 10
#define MAXDATASIZE 5 

using namespace llvm;
using namespace std;
using namespace SVF;

struct ToggleData {
    std::set<uint64_t> BBIds;
    std::string lineInfo;
    int targetID = 0;
};

#ifndef PANIC_BB_DEBUG
#define PANIC_BB_DEBUG
extern std::set<const BasicBlock*> panicBBs;
#endif

double jaccardSimilarity(const std::set<uint64_t>& set1, const std::set<uint64_t>& set2);
size_t blockCountDifference(const std::set<uint64_t>& set1, const std::set<uint64_t>& set2);
void mergeTargets(std::map<int, ToggleData>& targets, double jaccardThreshold, size_t sizeDiffThreshold);


bool is_subset(const std::set<uint64_t>& subset, const std::set<uint64_t>& superset);

/*Server helpers*/
void sigchld_handler(int);
void *get_in_addr(struct sockaddr *);

/*Fencing helpers*/
PTACallGraphNode* get_callgraph_node(SVFModule*, PTACallGraph*, std::string);
int64_t get_edge_id(uint32_t caller, uint32_t callee);
std::string get_func_name(std::map<uint32_t, std::set<std::string>>&, uint32_t);
const Function* get_function(SVFModule*);
const SVFFunction* get_svf_function(std::string functionName, SVFModule* svfModule);
bool isBlacklisted(const Function *);
bool isDroplisted(const Function *);
bool isCMLibrarylisted(const Function *F);
bool isThreadlisted(const Function *);
bool isAsynclisted(const Function *);
bool isUnsafelisted(const Function *);
void writeModuleToFile(llvm::Module *, const std::string &);
void read_and_sort_file(const std::string& , const std::string& );
void condense_file(const std::string& , const std::string& ) ;
void remove_subsets(const std::string& , const std::string& ) ;
std::set<int> union_sets(const std::vector<std::set<int>>& );
std::set<int> union_sets_from_file(const std::string& );
void write_union_result(const std::string& , const std::set<int>& );



/*Fencing helpers-debug */
void print_llvm_function(SVFModule*);
void print_svf_function(SVFModule*, PTACallGraph*);
void count_llvm_functions(SVFModule* );
void getmain(SVFModule* );
const SVFFunction* get_main_function(SVFModule*);
void get_allow_BB(ICFG* , const Instruction* );
std::set<const BasicBlock*> countAllBB(SVFModule* , ICFG*, int*, size_t);
std::set<const BasicBlock*> countAllBBAsyncSafe(SVFModule* , ICFG*, int*, size_t);
void countReachableBB(SVFModule* , ICFG* );
bool is_main_func(Function& );


using ICFGPath = std::vector<const llvm::Function*>;


/*Reachable Function from Rust Entry*/
bool isReachableFromRustEntry(PTACallGraphNode *);
bool isRustProgEntryFunction(const SVFFunction *);

bool isReachableForRust(const SVFFunction*, const SVFFunction*, PTACallGraph*);
bool isFunctionReachable(SVFModule* , PTACallGraph* , const Function* , const Function* );
void analyzeICFGWithPaths(SVFModule* , SVF::ICFG* , /*SVF::PTACallGraph* ,*/const std::string& ,const std::string&);
std::vector<ICFGPath> getPathsToTarget(const SVF::ICFG* ,const SVF::ICFGNode* ,const SVF::ICFGNode* );


const Instruction * get_main_inst(SVFModule*, size_t);
void getPtsFromInst(PointerAnalysis *, ICFG *, const Instruction *);

void process_indirect_edgeids(std::string&, std::set<uint64_t>&);
void process_indirect_callees(std::string&, std::set<uint64_t>&, std::set<std::string>&, const SVFFunction*, PTACallGraph*, SVFModule*);

/*Misc helpers*/
void dump_ids(std::string, std::set<uint32_t>);
void dump_num(std::string, std::size_t, std::size_t);
std::string demangleString(const char* name);

/*Fencing prototypes*/
std::set<std::string> traverseBackwardsFlowInsensitive(ICFG*, const Instruction*, PTACallGraph*);
std::set<std::string> traverseCallGraph(PTACallGraph*, PTACallGraphNode*);
bool isReachable(const SVFFunction*, const SVFFunction*, PTACallGraph*, std::set<uint64_t>&, std::set<std::string>&);
void traverseBackwardsFlowSensitiveForRust(ICFG* ,const ICFGNode* ,PTACallGraph* ,std::set<std::string>&);
void traverseBackwardsFlowSensitiveMode1(ICFG* ,const ICFGNode* ,PTACallGraph* ,std::set<std::string>& ,std::set<const Function*>&);


/*Fencing for Rust*/
void staticAnalysis(const SVFG*, const Instruction*, const SVFFunction*, PTACallGraph* );
void forwardAnalysis(const SVFGNode*, std::set<const SVFGNode*>&);
std::set<const ICFGNode*> backwardAnalysis(const SVFGNode*, const SVFFunction*, PTACallGraph*);
void getAllFuncs(SVFModule*, ICFG*);
std::set<const Function*> callgraphTraverse(PTACallGraphNode* );
std::vector<std::set<const SVFGNode*>> RawPtrInstructions(Module*, const SVFG*, PTACallGraph*);


std::set<const SVFGNode*> TrackAllocSites(const Instruction*, std::set<const SVFGEdge*> &, const SVFG*, PTACallGraph*);


/*Debug Helpers*/
void check_edge(SVFModule*, PTACallGraph*, std::string, std::string);
std::string printPts(PointerAnalysis* , const Value* );


/* Rust Static Algorithm Version 0 - for C/C++ debugging */
void backwardAnalysis_V0(int , ICFG * , PTACallGraph*);
void forwardAnalysis_V0(int , SVFG * , PTACallGraph* );

/* Rust Static Algorithm Version 1*/
std::set<const SVFGNode*> getAliasPtr(const SVFG* , const Value*, ofstream &);
void backwardAnalysis_V1(const SVFGNode* , std::set<const ICFGNode*>& , std::set<ICFGEdge*>& , PTACallGraph* , ofstream &);
void forwardAnalysis_V1(const SVFGNode* , std::set<const ICFGNode*>& checked_iNodes, std::set<ICFGEdge*>& , std::set<SVFGEdge*>& , PTACallGraph* , ofstream &);
void staticAnalysis_V1(SVFModule*, Module*, const SVFG* , ICFG* , PTACallGraph* );
void staticAnalysis_V1Debug(SVFModule*, const SVFG*, ICFG*, PTACallGraph*, const Instruction*);

/* Rust Sanitizer Static Analysis */
void ErasanAnalysis(SVFModule*, Module*, const SVFG*, ICFG*, PTACallGraph*);
void TrackAccessSites(const SVFGNode* , std::set<const SVFGNode*> &, std::set<const SVFGEdge*>& , ofstream &);
std::set<const SVFGNode*> SanitizerRawPtrInstructions(Module* , const SVFG* , PTACallGraph* );

/* Rust Sanitizer Static Analysis - OPT1 */
void ErasanAnalysis_opt(SVFModule* , Module* , const SVFG* , ICFG* , PTACallGraph* );
void ErasanAnalysis_thread_safe(SVFModule* , Module* , const SVFG* , ICFG* , PTACallGraph* );
void HeapOrStackIdentification(const SVFGNode*, std::set<const SVFGNode*> &, std::set<const SVFGNode*> &, std::set<const SVFGNode*> &);
void ParallelHeapOrStackIdentification(const SVFGNode* , std::set<const SVFGNode*> &, std::set<const SVFGNode*> &, std::set<const SVFGNode*> &, std::set<const SVFGNode*> &, std::mutex&);
void ParallelUserDefinedFunctionsIdentification(const std::set<const llvm::Function*> &,std::mutex &, std::set<const llvm::Function*> &);
void ParallelOutermostFunctionsIdentification(const std::set<const llvm::Function*> &, const std::set<const llvm::Function*> &, const std::set<const llvm::Function*> &, std::mutex &, std::set<const llvm::Function*> &, std::mutex &, std::set<std::string> &);
void ParallelS2FuzzHeapAccessSitesWrapper(const SVFGNode* , std::set<const ICFGNode*>& , std::mutex &, const std::set<std::string>& , std::set<const SVFGNode*>& , std::mutex &);
void ParallelS2FuzzTrackAccessSitesAfterDropOrig(const SVFGNode* , std::set<const ICFGNode*>& , std::set<const SVFGEdge*>& , const std::set<const llvm::Function*>& , const std::set<std::string>& , std::set<std::string>& , std::set<std::string>* , std::mutex &, std::mutex &);
void ParallelHeapOrStackIdentificationWrapper(std::set<const SVFGNode*>&, std::set<const SVFGNode*> &, std::set<const SVFGNode*> &, std::set<const SVFGNode*> &, std::set<const SVFGNode*> &, std::mutex& );
void SafeReferenceOptimization(std::set<const SVFGNode*> &, ofstream &);
void afterDropOpt(const SVFGNode* , ofstream &);
void TrackAccessSitesAfterDrop(const SVFGNode*, std::set<const SVFGEdge*> &);
void TrackAccessSitesAfterReturn(const SVFGNode* , std::set<const SVFGEdge*> &);
void setMetadata(std::set<const SVFGNode*>& , bool, ofstream &);
void TrackRawPtrSites(const Instruction* , const SVFG*);
void MarkHeapAllocationSites(const SVFGNode*);
void MarkHeapAccessSites(const SVFGNode*, std::set<const SVFGEdge*>&);
void ThreadSafeMarkHeapAccessSites(const SVFGNode*, std::set<const SVFGEdge*>& );
void MarkStackAllocationSites(const SVFGNode*);
void MarkStackAccessSites(const SVFGNode*, std::set<const SVFGEdge*>&);
void ThreadSafeMarkStackAccessSites(const SVFGNode*, std::set<const SVFGEdge*>&);


/* Rust Sanitizer Static Analysis - Unsafe Region Reachability Analysis */
std::set<const ICFGNode*> SanitizerUnsafeInstructions(Module*, const SVFG*, PTACallGraph*);
void ErasanAnalysis_unsafe_reachabilty(SVFModule*, Module*, const SVFG*, ICFG*, PTACallGraph*);
void backwardAnalysis_unsafe(const ICFGNode*, std::set<const ICFGNode*>&, std::set<ICFGEdge*>&, PTACallGraph*);
void forwardAnalysis_unsafe(const ICFGNode*, std::set<const ICFGNode*>&, std::set<ICFGEdge*>&, PTACallGraph*);
void ExternalUnsafeCheck(Module*);
bool containsUnsafeBlock(const std::string&, const int);

/* Rust Sanitizer Static Analysis - Unsafe Region Analysis with object tracking and memory access */
void ErasanAnalysis_unsafe_opt(SVFModule*, Module*, const SVFG*, ICFG*, PTACallGraph*);
std::set<const SVFGNode*> SanitizerUnsafeInstructions_OPT(Module*, const SVFG*, PTACallGraph*); 
void UnsafePreprocess(Module* );


/* S2Fuzz Static Analysis Implementation */
void S2FuzzStaticAnalysis(SVFModule* , Module* , const SVFG* , ICFG* , PTACallGraph*, size_t, bool, int, std::set<const BasicBlock*>&);
void ParallelS2FuzzStaticAnalysis(SVFModule* , Module* , const SVFG* , ICFG* , PTACallGraph*, size_t, bool, int, std::set<const BasicBlock*>&, int);
void S2FuzzHeapAccessSites(const SVFGNode*, std::set<const ICFGNode*>&, std::set<const SVFGEdge*>&, std::set<const llvm::Function *>, std::set<std::string>, std::set<std::string>*);
void ParallelS2FuzzHeapAccessSites(const SVFGNode*, std::set<const ICFGNode*>&, std::set<const SVFGEdge*>&, std::mutex &, std::set<const llvm::Function *>);
std::string extractDropInPlaceFunctionName(std::string& str);
void S2FuzzStackAccessSites(const SVFGNode*, std::set<const ICFGNode*>&, std::set<const SVFGEdge*>&);
void ParallelS2FuzzStackAccessSites(const SVFGNode*, std::set<const ICFGNode*>&, std::set<const SVFGEdge*>&, std::mutex &);
void S2FuzzTrackAccessSitesAfterDrop(const SVFGNode*, std::set<const ICFGNode*>&, std::set<const SVFGEdge*> &);
void S2FuzzTrackAccessSitesAfterDropOrig(const SVFGNode*, std::set<const ICFGNode*>&, std::set<const SVFGEdge*> &, std::set<const llvm::Function *>, std::set<std::string>, std::set<std::string>&, std::set<std::string>*);
void S2FuzzTrackAccessSitesAfterReturn(const SVFGNode* , std::set<const ICFGNode*>&, std::set<const SVFGEdge*> &);
std::set<const SVFGNode*> S2FuzzRawPtrInstructions(Module*, const SVFG*, PTACallGraph*, std::set<const ICFGNode*>&, std::set<const llvm::Function *>&, std::set<const llvm::Function *>&, std::set<std::string>&, std::set<const llvm::Function *>&);
// std::set<const ICFGNode*> S2FuzzBackwardAnalysis(const ICFGNode*, PTACallGraph*, const Function*);
std::set<const ICFGNode*> S2FuzzBackwardAnalysis(const ICFGNode*, PTACallGraph*, const Function*, std::set<const Function*>&);
std::set<const ICFGNode*> S2FuzzBackwardAnalysis_with_pruning_info(const ICFGNode*, PTACallGraph*, const Function*);
void ParallelBackwardAnalysis(const ICFGNode* , std::set<const BasicBlock*>& , int& , int&, std::mutex& , ostream &, ostream &, std::set<const llvm::BasicBlock *> &, 
                                SVF::PTACallGraph *, const llvm::Function *, std::mutex&, std::map<int, ToggleData> &, std::mutex&);
void ParallelBackwardAnalysisWrapper(std::set<const ICFGNode*>& , std::set<const BasicBlock*>& , int & , int & , std::mutex& , ostream &, ostream &, std::set<const llvm::BasicBlock *> &, 
                                SVF::PTACallGraph *, const llvm::Function *, std::mutex&, std::map<int, ToggleData> &, std::mutex&);
std::set<const Function*> TraverseCFGReachableFunc(const ICFGNode* ); 
void setTogglePoints(Module&);
void setTogglePointsAsyncSafe(Module&);
void setTogglePointsInputSharingMode(Module&);
void instrumentUnreachableFuncs(Module& , std::set<std::string>);
void traceConnectedBlocks(BasicBlock* , std::set<BasicBlock*>& );
void processHeapTargets(const std::set<const SVFGNode*>&, std::set<const ICFGNode*>&, std::set<const SVFGEdge*>&, std::mutex &, std::set<const llvm::Function *>);
void processStackTargets(const std::set<const SVFGNode*>&, std::set<const ICFGNode*>&, std::set<const SVFGEdge*>&, std::mutex &);

void S2FuzzManualTargetCollection(SVFModule* , Module* , const SVFG* , ICFG* , PTACallGraph* , const std::string &funcName, const std::string &bbName);
void S2FuzzManualTargetAnalysis(SVFModule*, Module*, const SVFG*, ICFG*, PTACallGraph*, int, std::set<const BasicBlock*>&, const std::vector<int>&);

/* Async Safe S2Fuzz Static Analysis */
void S2FuzzStaticAnalysis_async_safe(SVFModule* , Module* , const SVFG* , ICFG* , PTACallGraph* , size_t , bool , int , std::set<const BasicBlock*>& );
void S2FuzzTrackAccessSitesAfterDropAsyncSafe(const SVFGNode* , std::set<const ICFGNode*>& , std::set<const SVFGEdge*> &, std::set<const llvm::Function *> );
void S2FuzzTrackUnsafeHeapAccessSites(const SVFGNode* , std::set<const ICFGNode*>& , std::set<const SVFGEdge*> &, std::set<const llvm::Function *> );
void AsyncSafeS2FuzzHeapAccessSites(const SVFGNode* , std::set<const ICFGNode*>& , std::set<const SVFGEdge*>& , std::set<const llvm::Function *> , std::set<std::string> , std::set<std::string>* );
void S2FuzzTrackAccessSitesAfterReturnAsyncSafe(const SVFGNode* , std::set<const ICFGNode*>& , std::set<const SVFGEdge*> &);
void S2FuzzTrackUnsafeStackAccessSites(const SVFGNode* , std::set<const ICFGNode*>& , std::set<const SVFGEdge*> &);
void AsyncSafeS2FuzzStackAccessSites(const SVFGNode* , std::set<const ICFGNode*>& , std::set<const SVFGEdge*>& );




// void RustGoBaseModeAnalysis(SVFModule*, Module*, const SVFG*, ICFG*, PTACallGraph*, int, std::set<const BasicBlock*>&);
// void RustGoMode1Analysis(SVFModule* , Module* , const SVFG* , ICFG* , PTACallGraph* , int , std::set<const BasicBlock*>& );

// void RustGoBasicPruningAnalysis(SVFModule*, Module*, const SVFG*, ICFG*, PTACallGraph*, int, std::set<const BasicBlock*>&);


// void AblationModule1_HeapAccessSites(const SVFGNode* , std::set<const ICFGNode*>& , std::set<const SVFGEdge*>& );
// void AblationModule1_TrackAccessSitesAfterDrop(const SVFGNode* , std::set<const ICFGNode*>& , std::set<const SVFGEdge*> &);
// void AblationModule1_TrackAccessSitesAfterDropOrig(const SVFGNode* , std::set<const ICFGNode*>& , std::set<const SVFGEdge*> &);
// void AblationModule1(SVFModule* , Module* , const SVFG* , ICFG* , PTACallGraph* , int , std::set<const BasicBlock*>& );
// void AblationModule2(SVFModule* , Module* , const SVFG* , ICFG* , PTACallGraph* , int , std::set<const BasicBlock*>& );

void AblationStudyModuleBase(SVFModule*, Module*, const SVFG*, ICFG*, PTACallGraph*, int, std::set<const BasicBlock*>, const std::vector<int>&);
void DisableStdLibOpt(SVFModule*, Module*, const SVFG*, ICFG*, PTACallGraph*, int, std::set<const BasicBlock*>, const std::vector<int>&);
void NewVulAblationStudyModuleBase(SVFModule* , Module* , const SVFG* , ICFG* , PTACallGraph*, size_t, bool, int, std::set<const BasicBlock*>&);


/* Debug */
void countAllBB_debug(SVFModule* , Module* , ICFG* , size_t);



/* Rust Demangling */
const string RUST_LIBRARIES [] = {
        "alloc",
        "core",
        "std",
        "backtrace",
        "stdarch",
        "term",
        "rtstartup",
        "unwind",
        "libc",
        "cfg_if",
        "getrandom",
        "libm",
        "num_traits",
        "ppv_lite86",
        "proc_macro2",
        "proc_macro",
        "quote",
        "rand",
        "rand_chacha",
        "rand_core",
        "rand_distr",
        "rand_pcg",
        "serde",
        "serde_derive",
        "syn",
        "unicode_xid",
        "clap",
};

const string THREAD_FUNCTIONS [] = {
    "{{closure}}",
};


const string ASYNC_FUNCTIONS [] = {
    "GenFuture",
    "[static generator@",
    "{{closure}}",
    "from_generator",
};

const string UNSAFE_FUNCTIONS [] = {
    "get_unchecked",
};

extern set<const Function*> RustThreadFuncs;
extern set<const Function*> RustAsyncFuncs;
extern set<const Function*> RustUnsafeFuncs;

static bool isThreadFunc(const Function* func){
    return RustThreadFuncs.find(func) != RustThreadFuncs.end();
}

static bool isAsyncFunc(const Function* func){
    return RustAsyncFuncs.find(func) != RustAsyncFuncs.end();
}

static bool isUnsafeFunc(const Function* func){
    return RustUnsafeFuncs.find(func) != RustUnsafeFuncs.end();
}

static bool isThreadFunc(string FuncName) {

    if(FuncName.empty() || !FuncName.length())
        return false;
    string orig = FuncName;
    while(FuncName.length() > 1 && !isalpha(FuncName[0])){
        FuncName = FuncName.substr(1);
    }
    for(auto thread : THREAD_FUNCTIONS) {
        string compare_1 = thread+"::";
        if(FuncName.find(compare_1) != std::string::npos){
            return true;
        }
        if(FuncName.find("as "+compare_1,0) != std::string::npos){
            return true;
        }

    }
    return false;

}

static bool isAsyncFunc(string FuncName) {

    if(FuncName.empty() || !FuncName.length())
        return false;
    string orig = FuncName;
    while(FuncName.length() > 1 && !isalpha(FuncName[0])){
        FuncName = FuncName.substr(1);
    }
    for(auto async : ASYNC_FUNCTIONS) {
        string compare_1 = async+"::";
        if(FuncName.find(compare_1) != std::string::npos){
            return true;
        }
        if(FuncName.find("as "+compare_1,0) != std::string::npos){
            return true;
        }
        if(FuncName.find(async,0) != std::string::npos){
            return true;
        }

    }
    return false;

}

static bool isUnsafeFunc(string FuncName) {

    if(FuncName.empty() || !FuncName.length())
        return false;
    string orig = FuncName;
    while(FuncName.length() > 1 && !isalpha(FuncName[0])){
        FuncName = FuncName.substr(1);
    }
    for(auto unsafe : UNSAFE_FUNCTIONS) {
        string compare_1 = unsafe+"::";
        if(FuncName.find(compare_1) != std::string::npos){
            return true;
        }
        if(FuncName.find("as "+compare_1,0) != std::string::npos){
            return true;
        }
        if(FuncName.find(unsafe,0) != std::string::npos){
            return true;
        }

    }
    return false;

}


static void RustThreadFuncsMark(Function &F) {
    llvm::Attribute Attr = llvm::Attribute::get(F.getContext(), "RUST_THREAD_FUNCS");
    F.addFnAttr(Attr);
}

static void RustAsyncFuncsMark(Function &F) {
    llvm::Attribute Attr = llvm::Attribute::get(F.getContext(), "RUST_ASYNC_FUNCS");
    F.addFnAttr(Attr);
}

static void RustUnsafeFuncsMark(Function &F) {
    llvm::Attribute Attr = llvm::Attribute::get(F.getContext(), "RUST_UNSAFE_FUNCS");
    F.addFnAttr(Attr);
}


static void RustThreadMarking(Module& M){

    ofstream myfile;
    myfile.open("ThreadFuncsMarking.txt", ios::out | ios::trunc);

    char* demangle_buff = (char*) malloc(256);
    memset(demangle_buff,0,256);
    // char* demangle_buff_1 = (char*) malloc(256);
    // memset(demangle_buff_1,0,256);

    for(auto& F: M){
        if(F.isDeclaration())
            continue;
        string FuncName = F.getName().str();

        int demangle_result = demangle_func_name((char *) FuncName.c_str(), demangle_buff, 256);
        if (!demangle_result && isThreadFunc(demangle_buff)) {
            
            RustThreadFuncsMark(F);
            RustThreadFuncs.insert(&F);
            myfile << "Functions which have closure phrases : " << FuncName << "\n";
        }
        memset(demangle_buff,0,256);

    }
    myfile.close();
    free(demangle_buff);
    // free(demangle_buff_1);

}

static void RustAsyncFuncsMarking(Module& M){
    ofstream myfile;
    myfile.open("AsyncFuncsMarking.txt", ios::out | ios::trunc);

    char* demangle_buff = (char*) malloc(256);
    memset(demangle_buff,0,256);

    for(auto& F: M){
        if(F.isDeclaration())
            continue;
        string FuncName = F.getName().str();
        int demangle_result = demangle_func_name((char *) FuncName.c_str(), demangle_buff, 256);
        if (!demangle_result && isAsyncFunc(demangle_buff)) {
            RustAsyncFuncsMark(F);
            RustAsyncFuncs.insert(&F);
            myfile << "Functions which have Async phrases : " << FuncName << "\n";
        }
        memset(demangle_buff,0,256);
    }
    myfile.close();
    free(demangle_buff);
}

static void RustUnsafeFuncsMarking(Module& M){
    ofstream myfile;
    myfile.open("UnsafeFuncsMarking.txt", ios::out | ios::trunc);

    char* demangle_buff = (char*) malloc(256);
    memset(demangle_buff,0,256);

    for(auto& F: M){
        if(F.isDeclaration())
            continue;
        string FuncName = F.getName().str();
        int demangle_result = demangle_func_name((char *) FuncName.c_str(), demangle_buff, 256);
        if (!demangle_result && isUnsafeFunc(demangle_buff)) {
            RustUnsafeFuncsMark(F);
            RustUnsafeFuncs.insert(&F);
            myfile << "Functions which have Unsafe phrases : " << FuncName << "\n";
        }
        memset(demangle_buff,0,256);
    }
    myfile.close();
    free(demangle_buff);
}


//recheck
extern set<const Function*> RustDropStdLibraryFuncs;

static bool isRustDropLibraryFunc(const Function* func){
    return RustDropStdLibraryFuncs.find(func) != RustDropStdLibraryFuncs.end();
}
static bool isRustDropLibraryFunc(string FuncName){
    if(FuncName.empty() || !FuncName.length())
        return false;
    string orig = FuncName;
    while(FuncName.length() > 1 && !isalpha(FuncName[0])){
        FuncName = FuncName.substr(1);
    }

    //recheck
    for(auto lib: RUST_LIBRARIES){
        string compare = lib+"::";
        if(!strncmp(compare.c_str(), FuncName.c_str(), compare.length()) && (FuncName.find("drop_in_place") != std::string::npos || FuncName.find("free") != std::string::npos)){
            return true;
        }

        if(FuncName.find("as "+compare,0) != std::string::npos && (FuncName.find("drop_in_place") != std::string::npos || FuncName.find("free") != std::string::npos)){
            return true;
        }
    }
    return false;
}


extern set<const Function*> RustStdLibraryFuncs;

static bool isRustLibraryFunc(const Function* func){
    return RustStdLibraryFuncs.find(func) != RustStdLibraryFuncs.end();
}
static bool isRustLibraryFunc(string FuncName, const vector<string>& nonLibraryNames){
    if(FuncName.empty() || !FuncName.length())
        return false;
    string orig = FuncName;
    while(FuncName.length() > 1 && !isalpha(FuncName[0])){
        FuncName = FuncName.substr(1);
    }

    //recheck
    for(auto lib: RUST_LIBRARIES){
        string compare = lib+"::";
        if(!strncmp(compare.c_str(), FuncName.c_str(), compare.length())) {
            return true;
        }

        if(FuncName.find("as "+compare,0) != std::string::npos){
            return true;
        }
    }
    return false;
}

extern set<const Function*> RustCMLibraries;

static bool isRustCMLibraryFunc(const Function* func){
    return RustCMLibraries.find(func) != RustCMLibraries.end();
}
static bool isRustCMLibraryFunc(string FuncName, const vector<string>& nonLibraryNames){
    if(FuncName.empty() || !FuncName.length())
        return false;
    string orig = FuncName;
    while(FuncName.length() > 1 && !isalpha(FuncName[0])){
        FuncName = FuncName.substr(1);
    }
    

    for(const auto& name: nonLibraryNames) {
        string compare = name+"::";
        if(!strncmp(compare.c_str(), FuncName.c_str(), compare.length())){
            return true;
        }
    }

    return false;
}

static void RustCMStdWhitelistMarks(Function &F) {
    llvm::Attribute Attr = llvm::Attribute::get(F.getContext(), "RUST_CUSTOM_STANDARD_LIB");
    F.addFnAttr(Attr);
    // for(auto& BB: F) {
    //     for(llvm::BasicBlock::iterator I = BB.begin(), E = BB.end(); I != E; ++I) {
    //         Instruction *inst = &(*I);
    //         LLVMContext &context = inst->getContext();
    //         MDString *str = MDString::get(context, "whitelist");
    //         MDNode *node = MDNode::get(context, str);
    //         inst->setMetadata("whitelist", node);
    //     }
    // }
}


static void RustStdDroplistMarks(Function &F) {
    llvm::Attribute Attr = llvm::Attribute::get(F.getContext(), "RUST_STANDARD_DROP_LIB");
    F.addFnAttr(Attr);
    // for(auto& BB: F) {
    //     for(llvm::BasicBlock::iterator I = BB.begin(), E = BB.end(); I != E; ++I) {
    //         Instruction *inst = &(*I);
    //         LLVMContext &context = inst->getContext();
    //         MDString *str = MDString::get(context, "whitelist");
    //         MDNode *node = MDNode::get(context, str);
    //         inst->setMetadata("whitelist", node);
    //     }
    // }
}

static void RustStdWhitelistMarks(Function &F) {
    llvm::Attribute Attr = llvm::Attribute::get(F.getContext(), "RUST_STANDARD_LIB");
    F.addFnAttr(Attr);
    // for(auto& BB: F) {
    //     for(llvm::BasicBlock::iterator I = BB.begin(), E = BB.end(); I != E; ++I) {
    //         Instruction *inst = &(*I);
    //         LLVMContext &context = inst->getContext();
    //         MDString *str = MDString::get(context, "whitelist");
    //         MDNode *node = MDNode::get(context, str);
    //         inst->setMetadata("whitelist", node);
    //     }
    // }
}

// static std::set<const Function*> traverseSTDDirectCallfuncs(const Function& startFunction)
// {
//     std::set<const Function*> functions;

//     for (const BasicBlock &BB : startFunction) {
//         for (const Instruction &I : BB) {
//             if (const CallInst *callInst = dyn_cast<CallInst>(&I) ) {
//                     const Function* calledFunc = callInst->getCalledFunction();
//                     if(!calledFunc) continue;
//                     functions.insert(calledFunc);
//                 }

//                 else if (const InvokeInst *invokeInst = dyn_cast<InvokeInst>(&I)) {
//                     const Function* calledFunc = invokeInst->getCalledFunction();
//                     if(!calledFunc) continue;
//                     functions.insert(calledFunc);
//             }
//         }
//     }

//     return functions;

// }

static std::set<Function*> traverseSTDDirectCallfuncs(Function& startFunction) {
    std::set<Function*> functions;

    for (const BasicBlock& BB : startFunction) {
        for (const Instruction& I : BB) {
            if (const CallInst* callInst = dyn_cast<CallInst>(&I)) {
                if (Function* calledFunc = const_cast<Function*>(callInst->getCalledFunction())) {
                    functions.insert(calledFunc);
                }
            } else if (const InvokeInst* invokeInst = dyn_cast<InvokeInst>(&I)) {
                if (Function* calledFunc = const_cast<Function*>(invokeInst->getCalledFunction())) {
                    functions.insert(calledFunc);
                }
            }
        }
    }

    return functions;
}


static std::set<Function*> traverseSTDfuncs(PTACallGraphNode* startNode)
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


                if(functions.find(candidate) == functions.end() && !(candidate->isDeclaration())) {
                    functions.insert(candidate);
                }
                
                worklist.push(dstNode);
                visited.insert(dstNode);
                            
            }
        }

    }

    return functions;

}


static vector<string> getLLFileNames(const string& directory) {
    vector<string> names;
    regex ll_regex("([^/]+)-[^/]+\\.ll$");
    DIR* dir;
    struct dirent* ent;

    if ((dir = opendir(directory.c_str())) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            string filename = ent->d_name;
            smatch match;
            if (regex_search(filename, match, ll_regex)) {
                if (match.size() > 1) {
                    names.push_back(match.str(1));
                }
            }
        }
        closedir(dir);
    } else {
        perror("Could not open directory");
    }

    return names;
}

// Function to process constants and extract function pointers
static void processFunctionPointersInConstant(Constant *C, SVFModule* svfModule, PTACallGraph* callgraph) {
    if (C == nullptr) return;

    if (Function *F = dyn_cast<Function>(C)) {

        // Process functions connected via call graph if necessary
        const SVFFunction* svfFunc = svfModule->getSVFFunction(F);
        if (svfFunc) {
            PTACallGraphNode* callgraphNode = callgraph->getCallGraphNode(svfFunc);
            if (callgraphNode) {
                std::set<Function*> functions = traverseSTDfuncs(callgraphNode);
                for (auto& fun : functions) {
                    RustStdWhitelistMarks(*fun);
                    RustStdLibraryFuncs.insert(fun);
                }
            }
        }
        RustStdWhitelistMarks(*F);
        RustStdLibraryFuncs.insert(F);
    } else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(C)) {
        // Handle ConstantExpr, which could be casts, GEPs, etc.
        if (CE->isCast() || CE->getOpcode() == Instruction::GetElementPtr) {
            for (unsigned i = 0; i < CE->getNumOperands(); ++i) {
                processFunctionPointersInConstant(CE->getOperand(i), svfModule, callgraph);
            }
        }
    } else if (ConstantStruct *CS = dyn_cast<ConstantStruct>(C)) {
        // Process each operand of the struct
        for (unsigned i = 0; i < CS->getNumOperands(); ++i) {
            processFunctionPointersInConstant(CS->getOperand(i), svfModule, callgraph);
        }
    } else if (ConstantArray *CA = dyn_cast<ConstantArray>(C)) {
        // Process each element of the array
        for (unsigned i = 0; i < CA->getNumOperands(); ++i) {
            processFunctionPointersInConstant(CA->getOperand(i), svfModule, callgraph);
        }
    } else if (isa<ConstantAggregateZero>(C) || isa<ConstantPointerNull>(C)) {
        // Ignore zeros and null pointers
    } else {
        // Handle other constant types if necessary
    }
}

// Function to process global variables and extract function pointers
static void processGlobalVariables(Module &M, SVFModule* svfModule, PTACallGraph* callgraph) {
    for (GlobalVariable &GV : M.globals()) {
        if (GV.hasInitializer()) {
            Constant *init = GV.getInitializer();
            processFunctionPointersInConstant(init, svfModule, callgraph);
        }
    }
}


static void RustStdLibrariesMarking(Module& M, SVFModule* svfModule, PTACallGraph* callgraph){

    /* Handling Indirect Functions */
    processGlobalVariables(M, svfModule, callgraph);
    // for (GlobalVariable &GV : M.globals()) {
    //         if (GV.hasName() && GV.getName().startswith("vtable") && GV.hasInitializer()) {
    //             if (const ConstantStruct *CS = dyn_cast<ConstantStruct>(GV.getInitializer())) {
    //                 for (unsigned i = 0; i < CS->getNumOperands(); ++i) {
    //                     if (const Constant *C = dyn_cast<Constant>(CS->getOperand(i))) {
    //                         if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(C)) {
    //                             if (CE->isCast()) {
    //                                 if (Function *F = dyn_cast<Function>(CE->getOperand(0))) {

    //                                     /* Need to handle functions connected to Virtual Functions in callgraph */
    //                                     const SVFFunction* svfFunc = svfModule->getSVFFunction(F);
    //                                     PTACallGraphNode* callgraphNode = callgraph->getCallGraphNode(svfFunc);
    //                                     std::set<Function*> functions = traverseSTDfuncs(callgraphNode);
    //                                     for(auto& fun: functions){
    //                                         RustStdWhitelistMarks(*fun);
    //                                         RustStdLibraryFuncs.insert(fun);
    //                                     }
    //                                     RustStdWhitelistMarks(*F);
    //                                     RustStdLibraryFuncs.insert(F);
    //                                 }
    //                             }
    //                         }
    //                     }
    //                 }
    //             }
    //         }
        
    // }

    

    std::vector<std::string> nonLibraryNames = getLLFileNames("target/x86_64-unknown-linux-gnu/debug/deps");

    char* demangle_buff = (char*) malloc(256);
    memset(demangle_buff,0,256);
    for(auto& F: M){
        if(F.isDeclaration())
            continue;
        string FuncName = F.getName().str();

        int demangle_result = demangle_func_name((char *) FuncName.c_str(), demangle_buff, 256);
        if (!demangle_result && isRustLibraryFunc(demangle_buff, nonLibraryNames)) {
            // const SVFFunction* svfFunc = svfModule->getSVFFunction(&F);
            // PTACallGraphNode* callgraphNode = callgraph->getCallGraphNode(svfFunc);
            std::set<Function*> functions = traverseSTDDirectCallfuncs(F);
            for(auto& fun: functions){
                RustStdWhitelistMarks(*fun);
                RustStdLibraryFuncs.insert(fun);
            }

            RustStdWhitelistMarks(F);
            RustStdLibraryFuncs.insert(&F);
        }

        if(!demangle_result && isRustCMLibraryFunc(demangle_buff, nonLibraryNames)) {
            // const SVFFunction* svfFunc = svfModule->getSVFFunction(&F);
            // PTACallGraphNode* callgraphNode = callgraph->getCallGraphNode(svfFunc);
            std::set<Function*> functions = traverseSTDDirectCallfuncs(F);
            for(auto& fun: functions){
                RustCMStdWhitelistMarks(*fun);
                RustCMLibraries.insert(fun);
            }
            RustCMStdWhitelistMarks(F);
            RustCMLibraries.insert(&F);
        }


        //recheck
        if (!demangle_result && isRustDropLibraryFunc(demangle_buff)) {
            // const SVFFunction* svfFunc = svfModule->getSVFFunction(&F);
            // PTACallGraphNode* callgraphNode = callgraph->getCallGraphNode(svfFunc);
            std::set<Function*> functions = traverseSTDDirectCallfuncs(F);
            for(auto& fun: functions){
                RustStdDroplistMarks(*fun);
                RustDropStdLibraryFuncs.insert(fun);
            }

            RustStdDroplistMarks(F);
            RustDropStdLibraryFuncs.insert(&F);
        }        


        memset(demangle_buff,0,256);


        // for (auto& BB: F) {
        //     for (auto& inst: BB) {
        //         if(auto *callInst = dyn_cast<CallInst>(&inst)) {
        //             if(!callInst->getCalledFunction()) {
        //                 if(F.getName().str() != "_ZN4core4task4wake5Waker4wake17h16d98b0994a85095E") continue;
        //                 std::cout << "Indirect Call Detected : " << F.getName().str() << " " << BB.getName().str() << " " << callInst->getCalledOperand()->getName().str() << "\n";

        //                 // Indirect call detected
        //                 const Value* calledValue = callInst->getCalledOperand()->stripPointerCasts();
        //                 std::cout << "Called Value : " << calledValue->getName().str() << "\n";
        //                 PAG *pag = pta->getPAG();
        //                 if(!pag->hasValueNode(calledValue)) {
        //                     std::cout << "No Value Node\n";
        //                     continue;
        //                 }

        //                 NodeID valNode = pag->getValueNode(calledValue);
        //                 std::cout << "Value Node : " << valNode << "\n";
        //                 const PointsTo& pts = pta->getPTA()->getPts(valNode);
        //                 for (NodeID targetNode : pts) {
        //                     std::cout << "Target Node : " << targetNode << "\n";
        //                     const Value *targetValue = pag->getPAGNode(targetNode)->getValue();
        //                     if (const Function* targetFunc = dyn_cast<Function>(targetValue)) {
        //                         // std::cout << "Indirect Call Target : " << targetFunc->getName().str() << "\n";
        //                     }
        //                 }



        //             }
        //         }

        //         else if(auto *invokeInst = dyn_cast<InvokeInst>(&inst)) {
        //             if(!invokeInst->getCalledFunction()) {
        //                 // Indirect call detected
        //                 const Value* calledValue = invokeInst->getCalledOperand()->stripPointerCasts();
        //                 PAG *pag = pta->getPAG();
        //                 if(!pag->hasValueNode(calledValue)) {
        //                     continue;
        //                 }

        //                 NodeID valNode = pag->getValueNode(calledValue);
        //                 const PointsTo& pts = pta->getPTA()->getPts(valNode);
        //                 for (NodeID targetNode : pts) {
        //                     const Value *targetValue = pag->getPAGNode(targetNode)->getValue();
        //                     if (const Function* targetFunc = dyn_cast<Function>(targetValue)) {
        //                         // std::cout << "Indirect Call Target : " << targetFunc->getName().str() << "\n";
        //                     }
        //                 }



        //             }
        //         }

                
        //     }
        // }


    }
    free(demangle_buff);


    // ofstream stdlibs;
    // stdlibs.open("stdlibs.txt", ios::out | ios::trunc);
    // for(auto i = RustStdLibraryFuncs.begin(); i != RustStdLibraryFuncs.end(); i++) {
    //     stdlibs << "STD LIB : " << (*i)->getName().str() << "\n";
    // }
    // for(auto i = RustCMLibraries.begin(); i != RustCMLibraries.end(); i++) {
    //     stdlibs << "CM LIB : " << (*i)->getName().str() << "\n";
    // }
    // for(auto i = RustDropStdLibraryFuncs.begin(); i != RustDropStdLibraryFuncs.end(); i++) {
    //     stdlibs << "DROP LIB : " << (*i)->getName().str() << "\n";
    // }
    // stdlibs.close();


}