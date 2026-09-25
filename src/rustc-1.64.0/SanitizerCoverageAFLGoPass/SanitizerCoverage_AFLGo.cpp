//===-- SanitizerCoverage.cpp - coverage instrumentation for sanitizers ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Coverage instrumentation done on LLVM IR level, works with Sanitizers.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/SanitizerCoverage.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/EHPersonalities.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/SpecialCaseList.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"


/*##############################*/
/*##  aflgo-pass-HeaderFile  ###*/
/*##############################*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <list>
#include <map>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Analysis/CFGPrinter.h"
#include "types.h"
#include "config.h"
#include "debug.h"
// #include "/home/uni20171281/aflgo/config.h"
// #include "/home/uni20171281/aflgo/debug.h"

#if defined(LLVM34)
#include "llvm/DebugInfo.h"
#else
#include "llvm/IR/DebugInfo.h"
#endif

#define AFL_R(x) (random() % (x))

#if defined(LLVM34) || defined(LLVM35) || defined(LLVM36)
#define LLVM_OLD_DEBUG_API
#endif


/*###############################*/
/*####  aflgo-pass-Option  ######*/
/*###############################*/
using namespace llvm;

cl::opt<std::string> DistanceFile(
    "distance",
    cl::desc("Distance file containing the distance of each basic block to the provided targets."),
    cl::value_desc("filename")
);

cl::opt<std::string> TargetsFile(
    "targets",
    cl::desc("Input file containing the target lines of code."),
    cl::value_desc("targets"));

cl::opt<std::string> OutDirectory(
    "outdir",
    cl::desc("Output directory where Ftargets.txt, Fnames.txt, and BBnames.txt are generated."),
    cl::value_desc("outdir"));


/*###############################*/
/*####  aflgo-pass-struct  ######*/
/*###############################*/
namespace llvm {

template<>
struct DOTGraphTraits<Function*> : public DefaultDOTGraphTraits {
  DOTGraphTraits(bool isSimple=true) : DefaultDOTGraphTraits(isSimple) {}

  static std::string getGraphName(Function *F) {
    return "CFG for '" + F->getName().str() + "' function";
  }

  std::string getNodeLabel(BasicBlock *Node, Function *Graph) {
    if (!Node->getName().empty()) {
      return Node->getName().str();
    }

    std::string Str;
    raw_string_ostream OS(Str);

    Node->printAsOperand(OS, false);
    return OS.str();
  }
};

} // namespace llvm


/*###############################*/
/*#####  aflgo-pass-Class  ######*/
/*###############################*/
namespace {

  class AFLCoverage : public ModulePass {

    public:

      static char ID;
      AFLCoverage() : ModulePass(ID) { }

      bool runOnModule(Module &M) override;

      // StringRef getPassName() const override {
      //  return "American Fuzzy Lop Instrumentation";
      // }

  };

}


/*#################################*/
/*## aflgo-pass-Global-Variable  ##*/
/*#################################*/
char AFLCoverage::ID = 0;


/*###################################*/
/*## aflgo-pass-Fuction-Prototype  ##*/
/*###################################*/
static void getDebugLoc(const Instruction *I, std::string &Filename,unsigned &Line);
static bool isBlacklisted(const Function *F);



using namespace llvm;

#define DEBUG_TYPE "sancov"

const char SanCovTracePCIndirName[] = "__sanitizer_cov_trace_pc_indir";
const char SanCovTracePCName[] = "__sanitizer_cov_trace_pc";
const char SanCovTraceCmp1[] = "__sanitizer_cov_trace_cmp1";
const char SanCovTraceCmp2[] = "__sanitizer_cov_trace_cmp2";
const char SanCovTraceCmp4[] = "__sanitizer_cov_trace_cmp4";
const char SanCovTraceCmp8[] = "__sanitizer_cov_trace_cmp8";
const char SanCovTraceConstCmp1[] = "__sanitizer_cov_trace_const_cmp1";
const char SanCovTraceConstCmp2[] = "__sanitizer_cov_trace_const_cmp2";
const char SanCovTraceConstCmp4[] = "__sanitizer_cov_trace_const_cmp4";
const char SanCovTraceConstCmp8[] = "__sanitizer_cov_trace_const_cmp8";
const char SanCovLoad1[] = "__sanitizer_cov_load1";
const char SanCovLoad2[] = "__sanitizer_cov_load2";
const char SanCovLoad4[] = "__sanitizer_cov_load4";
const char SanCovLoad8[] = "__sanitizer_cov_load8";
const char SanCovLoad16[] = "__sanitizer_cov_load16";
const char SanCovStore1[] = "__sanitizer_cov_store1";
const char SanCovStore2[] = "__sanitizer_cov_store2";
const char SanCovStore4[] = "__sanitizer_cov_store4";
const char SanCovStore8[] = "__sanitizer_cov_store8";
const char SanCovStore16[] = "__sanitizer_cov_store16";
const char SanCovTraceDiv4[] = "__sanitizer_cov_trace_div4";
const char SanCovTraceDiv8[] = "__sanitizer_cov_trace_div8";
const char SanCovTraceGep[] = "__sanitizer_cov_trace_gep";
const char SanCovTraceSwitchName[] = "__sanitizer_cov_trace_switch";
const char SanCovModuleCtorTracePcGuardName[] =
    "sancov.module_ctor_trace_pc_guard";
const char SanCovModuleCtor8bitCountersName[] =
    "sancov.module_ctor_8bit_counters";
const char SanCovModuleCtorBoolFlagName[] = "sancov.module_ctor_bool_flag";
static const uint64_t SanCtorAndDtorPriority = 2;

const char SanCovTracePCGuardName[] = "__sanitizer_cov_trace_pc_guard";
const char SanCovTracePCGuardInitName[] = "__sanitizer_cov_trace_pc_guard_init";
const char SanCov8bitCountersInitName[] = "__sanitizer_cov_8bit_counters_init";
const char SanCovBoolFlagInitName[] = "__sanitizer_cov_bool_flag_init";
const char SanCovPCsInitName[] = "__sanitizer_cov_pcs_init";

const char SanCovGuardsSectionName[] = "sancov_guards";
const char SanCovCountersSectionName[] = "sancov_cntrs";
const char SanCovBoolFlagSectionName[] = "sancov_bools";
const char SanCovPCsSectionName[] = "sancov_pcs";

const char SanCovLowestStackName[] = "__sancov_lowest_stack";

static cl::opt<int> ClCoverageLevel(
    "sanitizer-coverage-level",
    cl::desc("Sanitizer Coverage. 0: none, 1: entry block, 2: all blocks, "
             "3: all blocks and critical edges"),
    cl::Hidden, cl::init(0));

static cl::opt<bool> ClTracePC("sanitizer-coverage-trace-pc",
                               cl::desc("Experimental pc tracing"), cl::Hidden,
                               cl::init(false));

static cl::opt<bool> ClTracePCGuard("sanitizer-coverage-trace-pc-guard",
                                    cl::desc("pc tracing with a guard"),
                                    cl::Hidden, cl::init(false));

// If true, we create a global variable that contains PCs of all instrumented
// BBs, put this global into a named section, and pass this section's bounds
// to __sanitizer_cov_pcs_init.
// This way the coverage instrumentation does not need to acquire the PCs
// at run-time. Works with trace-pc-guard, inline-8bit-counters, and
// inline-bool-flag.
static cl::opt<bool> ClCreatePCTable("sanitizer-coverage-pc-table",
                                     cl::desc("create a static PC table"),
                                     cl::Hidden, cl::init(false));

static cl::opt<bool>
    ClInline8bitCounters("sanitizer-coverage-inline-8bit-counters",
                         cl::desc("increments 8-bit counter for every edge"),
                         cl::Hidden, cl::init(false));

static cl::opt<bool>
    ClInlineBoolFlag("sanitizer-coverage-inline-bool-flag",
                     cl::desc("sets a boolean flag for every edge"), cl::Hidden,
                     cl::init(false));

static cl::opt<bool>
    ClCMPTracing("sanitizer-coverage-trace-compares",
                 cl::desc("Tracing of CMP and similar instructions"),
                 cl::Hidden, cl::init(false));

static cl::opt<bool> ClDIVTracing("sanitizer-coverage-trace-divs",
                                  cl::desc("Tracing of DIV instructions"),
                                  cl::Hidden, cl::init(false));

static cl::opt<bool> ClLoadTracing("sanitizer-coverage-trace-loads",
                                   cl::desc("Tracing of load instructions"),
                                   cl::Hidden, cl::init(false));

static cl::opt<bool> ClStoreTracing("sanitizer-coverage-trace-stores",
                                    cl::desc("Tracing of store instructions"),
                                    cl::Hidden, cl::init(false));

static cl::opt<bool> ClGEPTracing("sanitizer-coverage-trace-geps",
                                  cl::desc("Tracing of GEP instructions"),
                                  cl::Hidden, cl::init(false));

static cl::opt<bool>
    ClPruneBlocks("sanitizer-coverage-prune-blocks",
                  cl::desc("Reduce the number of instrumented blocks"),
                  cl::Hidden, cl::init(true));

static cl::opt<bool> ClStackDepth("sanitizer-coverage-stack-depth",
                                  cl::desc("max stack depth tracing"),
                                  cl::Hidden, cl::init(false));

namespace {

SanitizerCoverageOptions getOptions(int LegacyCoverageLevel) {
  SanitizerCoverageOptions Res;
  switch (LegacyCoverageLevel) {
  case 0:
    Res.CoverageType = SanitizerCoverageOptions::SCK_None;
    break;
  case 1:
    Res.CoverageType = SanitizerCoverageOptions::SCK_Function;
    break;
  case 2:
    Res.CoverageType = SanitizerCoverageOptions::SCK_BB;
    break;
  case 3:
    Res.CoverageType = SanitizerCoverageOptions::SCK_Edge;
    break;
  case 4:
    Res.CoverageType = SanitizerCoverageOptions::SCK_Edge;
    Res.IndirectCalls = true;
    break;
  }
  return Res;
}

SanitizerCoverageOptions OverrideFromCL(SanitizerCoverageOptions Options) {
  // Sets CoverageType and IndirectCalls.
  SanitizerCoverageOptions CLOpts = getOptions(ClCoverageLevel);
  Options.CoverageType = std::max(Options.CoverageType, CLOpts.CoverageType);
  Options.IndirectCalls |= CLOpts.IndirectCalls;
  Options.TraceCmp |= ClCMPTracing;
  Options.TraceDiv |= ClDIVTracing;
  Options.TraceGep |= ClGEPTracing;
  Options.TracePC |= ClTracePC;
  Options.TracePCGuard |= ClTracePCGuard;
  Options.Inline8bitCounters |= ClInline8bitCounters;
  Options.InlineBoolFlag |= ClInlineBoolFlag;
  Options.PCTable |= ClCreatePCTable;
  Options.NoPrune |= !ClPruneBlocks;
  Options.StackDepth |= ClStackDepth;
  Options.TraceLoads |= ClLoadTracing;
  Options.TraceStores |= ClStoreTracing;
  if (!Options.TracePCGuard && !Options.TracePC &&
      !Options.Inline8bitCounters && !Options.StackDepth &&
      !Options.InlineBoolFlag && !Options.TraceLoads && !Options.TraceStores)
    Options.TracePCGuard = true; // TracePCGuard is default.
  return Options;
}

using DomTreeCallback = function_ref<const DominatorTree *(Function &F)>;
using PostDomTreeCallback =
    function_ref<const PostDominatorTree *(Function &F)>;

class ModuleSanitizerCoverage {
public:
  ModuleSanitizerCoverage(
      const SanitizerCoverageOptions &Options = SanitizerCoverageOptions(),
      const SpecialCaseList *Allowlist = nullptr,
      const SpecialCaseList *Blocklist = nullptr)
      : Options(OverrideFromCL(Options)), Allowlist(Allowlist),
        Blocklist(Blocklist) {}
  bool instrumentModule(Module &M, DomTreeCallback DTCallback,
                        PostDomTreeCallback PDTCallback);

private:
  void instrumentFunction(Function &F, DomTreeCallback DTCallback,
                          PostDomTreeCallback PDTCallback);
  void InjectCoverageForIndirectCalls(Function &F,
                                      ArrayRef<Instruction *> IndirCalls);
  void InjectTraceForCmp(Function &F, ArrayRef<Instruction *> CmpTraceTargets);
  void InjectTraceForDiv(Function &F,
                         ArrayRef<BinaryOperator *> DivTraceTargets);
  void InjectTraceForGep(Function &F,
                         ArrayRef<GetElementPtrInst *> GepTraceTargets);
  void InjectTraceForLoadsAndStores(Function &F, ArrayRef<LoadInst *> Loads,
                                    ArrayRef<StoreInst *> Stores);
  void InjectTraceForSwitch(Function &F,
                            ArrayRef<Instruction *> SwitchTraceTargets);
  bool InjectCoverage(Function &F, ArrayRef<BasicBlock *> AllBlocks,
                      bool IsLeafFunc = true);
  GlobalVariable *CreateFunctionLocalArrayInSection(size_t NumElements,
                                                    Function &F, Type *Ty,
                                                    const char *Section);
  GlobalVariable *CreatePCArray(Function &F, ArrayRef<BasicBlock *> AllBlocks);
  void CreateFunctionLocalArrays(Function &F, ArrayRef<BasicBlock *> AllBlocks);
  void InjectCoverageAtBlock(Function &F, BasicBlock &BB, size_t Idx,
                             bool IsLeafFunc = true);
  Function *CreateInitCallsForSections(Module &M, const char *CtorName,
                                       const char *InitFunctionName, Type *Ty,
                                       const char *Section);
  std::pair<Value *, Value *> CreateSecStartEnd(Module &M, const char *Section,
                                                Type *Ty);

  void SetNoSanitizeMetadata(Instruction *I) {
    I->setMetadata(I->getModule()->getMDKindID("nosanitize"),
                   MDNode::get(*C, None));
  }

  std::string getSectionName(const std::string &Section) const;
  std::string getSectionStart(const std::string &Section) const;
  std::string getSectionEnd(const std::string &Section) const;
  FunctionCallee SanCovTracePCIndir;
  FunctionCallee SanCovTracePC, SanCovTracePCGuard;
  std::array<FunctionCallee, 4> SanCovTraceCmpFunction;
  std::array<FunctionCallee, 4> SanCovTraceConstCmpFunction;
  std::array<FunctionCallee, 5> SanCovLoadFunction;
  std::array<FunctionCallee, 5> SanCovStoreFunction;
  std::array<FunctionCallee, 2> SanCovTraceDivFunction;
  FunctionCallee SanCovTraceGepFunction;
  FunctionCallee SanCovTraceSwitchFunction;
  GlobalVariable *SanCovLowestStack;
  Type *Int128PtrTy, *IntptrTy, *IntptrPtrTy, *Int64Ty, *Int64PtrTy, *Int32Ty,
      *Int32PtrTy, *Int16PtrTy, *Int16Ty, *Int8Ty, *Int8PtrTy, *Int1Ty,
      *Int1PtrTy;
  Module *CurModule;
  std::string CurModuleUniqueId;
  Triple TargetTriple;
  LLVMContext *C;
  const DataLayout *DL;

  GlobalVariable *FunctionGuardArray;  // for trace-pc-guard.
  GlobalVariable *Function8bitCounterArray;  // for inline-8bit-counters.
  GlobalVariable *FunctionBoolArray;         // for inline-bool-flag.
  GlobalVariable *FunctionPCsArray;  // for pc-table.
  SmallVector<GlobalValue *, 20> GlobalsToAppendToUsed;
  SmallVector<GlobalValue *, 20> GlobalsToAppendToCompilerUsed;

  SanitizerCoverageOptions Options;

  const SpecialCaseList *Allowlist;
  const SpecialCaseList *Blocklist;
};

class ModuleSanitizerCoverageLegacyPass : public ModulePass {
public:
  ModuleSanitizerCoverageLegacyPass(
      const SanitizerCoverageOptions &Options = SanitizerCoverageOptions(),
      const std::vector<std::string> &AllowlistFiles =
          std::vector<std::string>(),
      const std::vector<std::string> &BlocklistFiles =
          std::vector<std::string>())
      : ModulePass(ID), Options(Options) {
    if (AllowlistFiles.size() > 0)
      Allowlist = SpecialCaseList::createOrDie(AllowlistFiles,
                                               *vfs::getRealFileSystem());
    if (BlocklistFiles.size() > 0)
      Blocklist = SpecialCaseList::createOrDie(BlocklistFiles,
                                               *vfs::getRealFileSystem());
    initializeModuleSanitizerCoverageLegacyPassPass(
        *PassRegistry::getPassRegistry());
  }

  bool runOnModule(Module &M) override{
    llvm::errs() << "SanitizerCoverage Fuction 1 : runOnModule\n\n\n";

    ModuleSanitizerCoverage ModuleSancov(Options, Allowlist.get(),
                                         Blocklist.get());
    auto DTCallback = [this](Function &F) -> const DominatorTree * {
      return &this->getAnalysis<DominatorTreeWrapperPass>(F).getDomTree();
    };
    auto PDTCallback = [this](Function &F) -> const PostDominatorTree * {
      return &this->getAnalysis<PostDominatorTreeWrapperPass>(F)
                  .getPostDomTree();
    };
    return ModuleSancov.instrumentModule(M, DTCallback, PDTCallback);
  }

  static char ID; // Pass identification, replacement for typeid
  StringRef getPassName() const override { return "ModuleSanitizerCoverage"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<PostDominatorTreeWrapperPass>();
  }

private:
  SanitizerCoverageOptions Options;

  std::unique_ptr<SpecialCaseList> Allowlist;
  std::unique_ptr<SpecialCaseList> Blocklist;
};

} // namespace

// PreservedAnalyses ModuleSanitizerCoveragePass::run(Module &M,
//                                                    ModuleAnalysisManager &MAM) {

//   llvm::errs() << "SanitizerCoverage Fuction 2 : run\n\n\n";

//   /*########################*/
//   /*##  aflgo-runOnModule ##*/
//   /*########################*/
//   bool is_aflgo = false;
//   bool is_aflgo_preprocessing = false;
//   if (!TargetsFile.empty() && !DistanceFile.empty()) {
//     FATAL("Cannot specify both '-targets' and '-distance'!");
//     return PreservedAnalyses::none();
//   }

//   std::list<std::string> targets;
//   std::map<std::string, int> bb_to_dis;
//   std::vector<std::string> basic_blocks;

//   if (!TargetsFile.empty()) {

//     if (OutDirectory.empty()) {
//       FATAL("Provide output directory '-outdir <directory>'");
//       return PreservedAnalyses::none();
//     }

//     std::ifstream targetsfile(TargetsFile);
//     std::string line;
//     while (std::getline(targetsfile, line))
//       targets.push_back(line);
//     targetsfile.close();

//     is_aflgo_preprocessing = true;
//   } else if (!DistanceFile.empty()) {

//     std::ifstream cf(DistanceFile);
//     if (cf.is_open()) {

//       std::string line;

//       while (getline(cf, line)) {
//         std::size_t pos = line.find(",");
//         std::string bb_name = line.substr(0, pos);
//         int bb_dis = (int) (100.0 * atof(line.substr(pos + 1, line.length()).c_str()));
//         bb_to_dis.emplace(bb_name, bb_dis);
//         basic_blocks.push_back(bb_name);
//       }
//       cf.close();
//       is_aflgo = true;

//     } else {
//       FATAL("Unable to find %s.", DistanceFile.c_str());
//       return PreservedAnalyses::none();
//     }
//   }

//   /* Show a banner */
//   char be_quiet = 0;
//   if (isatty(2) && !getenv("AFL_QUIET")) {
//     if (is_aflgo || is_aflgo_preprocessing)
//       SAYF(cCYA "aflgo-llvm-pass (yeah!) " cBRI VERSION cRST " (%s mode)\n",
//            (is_aflgo_preprocessing ? "preprocessing" : "distance instrumentation"));
//     else
//       SAYF(cCYA "afl-llvm-pass " cBRI VERSION cRST " by <lszekeres@google.com>\n");
//   } else be_quiet = 1;

//   /* Decide instrumentation ratio */
//   char* inst_ratio_str = getenv("AFL_INST_RATIO");
//   unsigned int inst_ratio = 100;
//   if (inst_ratio_str) {
//     if (sscanf(inst_ratio_str, "%u", &inst_ratio) != 1 || !inst_ratio ||
//         inst_ratio > 100)
//       FATAL("Bad value of AFL_INST_RATIO (must be between 1 and 100)");
//   }
//     /* Default: Not selective */
//   char* is_selective_str = getenv("AFLGO_SELECTIVE");
//   unsigned int is_selective = 0;
//   if (is_selective_str && sscanf(is_selective_str, "%u", &is_selective) != 1)
//     FATAL("Bad value of AFLGO_SELECTIVE (must be 0 or 1)");
//   char* dinst_ratio_str = getenv("AFLGO_INST_RATIO");
//   unsigned int dinst_ratio = 100;
//   if (dinst_ratio_str) {
//     if (sscanf(dinst_ratio_str, "%u", &dinst_ratio) != 1 || !dinst_ratio ||
//         dinst_ratio > 100)
//       FATAL("Bad value of AFLGO_INST_RATIO (must be between 1 and 100)");
//   }

//   int inst_blocks = 0;

//   /*##########################*/
//   /*##  aflgo-preprocessing ##*/
//   /*##########################*/
//   if(is_aflgo_preprocessing){
    
//     std::ofstream bbnames(OutDirectory + "/BBnames.txt", std::ofstream::out | std::ofstream::app);
//     std::ofstream bbcalls(OutDirectory + "/BBcalls.txt", std::ofstream::out | std::ofstream::app);
//     std::ofstream fnames(OutDirectory + "/Fnames.txt", std::ofstream::out | std::ofstream::app);
//     std::ofstream ftargets(OutDirectory + "/Ftargets.txt", std::ofstream::out | std::ofstream::app);

//     /* Create dot-files directory */
//     std::string dotfiles(OutDirectory + "/dot-files");
//     if (sys::fs::create_directory(dotfiles)) {
//       FATAL("Could not create directory %s.", dotfiles.c_str());
//     }

//     for (auto &F : M) {

//       bool has_BBs = false;
//       std::string funcName = F.getName().str();
//       llvm::errs() << "funcName is "<<funcName << "\n\n\n\n";

//       /* Black list of function names */
//       // if (isBlacklisted(&F)) {
//       //   continue;
//       // }

//       bool is_target = false;
//       for (auto &BB : F) {

//         std::string bb_name("");
//         std::string filename;
//         unsigned line;

//         for (auto &I : BB) {
//           getDebugLoc(&I, filename, line);

//           /* Don't worry about external libs */
//           static const std::string Xlibs("/usr/");
//           if (filename.empty() || line == 0 || !filename.compare(0, Xlibs.size(), Xlibs))
//             continue;

//           if (bb_name.empty()) {

//             std::size_t found = filename.find_last_of("/\\");
//             if (found != std::string::npos)
//               filename = filename.substr(found + 1);

//             bb_name = filename + ":" + std::to_string(line);
//           }
//           llvm::errs()<<"BB name is "<<bb_name<<"\n\n\n";

//           if (!is_target) {
//               for (auto &target : targets) {
//                 std::size_t found = target.find_last_of("/\\");
//                 if (found != std::string::npos)
//                   target = target.substr(found + 1);

//                 std::size_t pos = target.find_last_of(":");
//                 std::string target_file = target.substr(0, pos);
//                 unsigned int target_line = atoi(target.substr(pos + 1).c_str());

//                 if (!target_file.compare(filename) && target_line == line)
//                   is_target = true;

//               }
//             }

//             if (auto *c = dyn_cast<CallInst>(&I)) {

//               std::size_t found = filename.find_last_of("/\\");
//               if (found != std::string::npos)
//                 filename = filename.substr(found + 1);

//               if (auto *CalledF = c->getCalledFunction()) {
//                 if (!isBlacklisted(CalledF))
//                   bbcalls << bb_name << "," << CalledF->getName().str() << "\n";
//               }
//             }

//         }

//         if (!bb_name.empty()) {

//           BB.setName(bb_name + ":");
//           if (!BB.hasName()) {
//             std::string newname = bb_name + ":";
//             Twine t(newname);
//             SmallString<256> NameData;
//             StringRef NameRef = t.toStringRef(NameData);
//             MallocAllocator Allocator;
//             BB.setValueName(ValueName::Create(NameRef, Allocator));
//           }

//           bbnames << BB.getName().str() << "\n";
//           has_BBs = true;

// #ifdef AFLGO_TRACING
//           auto *TI = BB.getTerminator();
//           IRBuilder<> Builder(TI);

//           Value *bbnameVal = Builder.CreateGlobalStringPtr(bb_name);
//           Type *Args[] = {
//               Type::getInt8PtrTy(M.getContext()) //uint8_t* bb_name
//           };
//           FunctionType *FTy = FunctionType::get(Type::getVoidTy(M.getContext()), Args, false);
//           Constant *instrumented = M.getOrInsertFunction("llvm_profiling_call", FTy);
//           Builder.CreateCall(instrumented, {bbnameVal});
// #endif
//         }
//       }
//       if (has_BBs) {
//         /* Print CFG */
//         std::string cfgFileName = dotfiles + "/cfg." + funcName + ".dot";
//         std::error_code EC;
//         raw_fd_ostream cfgFile(cfgFileName, EC, sys::fs::OF_None);
//         if (!EC) {
//           WriteGraph(cfgFile, &F, true);
//         }

//         if (is_target)
//           ftargets << F.getName().str() << "\n";
//         fnames << F.getName().str() << "\n";
//       }
//       llvm::errs() << "ftargets are "<<F.getName().str()<<"\n\n\n";
//       llvm::errs() << "fnames are "<<F.getName().str()<<"\n\n\n";

//     }
//   }
//   else{
//     /*################################*/
//     /*##  aflgo-distance-instrument ##*/
//     /*################################*/
//     LLVMContext &C = M.getContext();
//     IntegerType *Int8Ty  = IntegerType::getInt8Ty(C);
//     IntegerType *Int32Ty = IntegerType::getInt32Ty(C);
//     IntegerType *Int64Ty = IntegerType::getInt64Ty(C);

    
//   } 

//   /*#############################*/
//   /*##  SanitiizerCoverage-run ##*/
//   /*#############################*/
//   ModuleSanitizerCoverage ModuleSancov(Options, Allowlist.get(),
//                                        Blocklist.get());
//   auto &FAM = MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
//   auto DTCallback = [&FAM](Function &F) -> const DominatorTree * {
//     return &FAM.getResult<DominatorTreeAnalysis>(F);
//   };
//   auto PDTCallback = [&FAM](Function &F) -> const PostDominatorTree * {
//     return &FAM.getResult<PostDominatorTreeAnalysis>(F);
//   };
//   if (ModuleSancov.instrumentModule(M, DTCallback, PDTCallback))
//     return PreservedAnalyses::none();
//   return PreservedAnalyses::all();


// }

// PreservedAnalyses ModuleSanitizerCoveragePass::run(Module &M, ModuleAnalysisManager &MAM) {
//   llvm::errs() << "--- run point --- \n\n";
//   llvm::errs() << "< each module name : " << M.getName().str() << ">\n\n";
//   for (auto &F: M) {
//     llvm::errs() << "< each Function name : " << F.getName().str() << ">\n\n";
//     std::string bb_name("");
//     std::string filename;
//     unsigned line;

//     for (auto &BB: F) {
      
//       for (auto &I : BB) {
//         if(DILocation *Loc = I.getDebugLoc()) {
//           line = Loc->getLine();
//           filename = Loc->getFilename().str();
//           if (filename != "main.rs") continue;
//           // llvm::errs() << "file name : " << filename <<"\n\n";
//           // llvm::errs() << "line : " << line << "\n\n";
//         }
//         if (bb_name.empty()) {

//           std::size_t found = filename.find_last_of("/\\");
//           if (found != std::string::npos)
//             filename = filename.substr(found + 1);

//           bb_name = filename + ":" + std::to_string(line);
//         }
        
//       }
//       if(!bb_name.empty()) {
//         BB.setName(bb_name + ":");
//       }


//     }
//   }
//   return PreservedAnalyses::all(); 
// }

/*#############################*/
/*#############################*/
/*#############################*/
/*#############################*/
/* ydy code starts */
/*#############################*/
/*#############################*/
/*#############################*/
/*#############################*/

PreservedAnalyses ModuleSanitizerCoveragePass::run(Module &M, ModuleAnalysisManager &MAM) {

  bool is_aflgo = false;
  bool is_aflgo_preprocessing = false;

  if (!TargetsFile.empty() && !DistanceFile.empty()) {
    FATAL("Cannot specify both '-targets' and '-distance'!");
    return PreservedAnalyses::none();
  }

  std::list<std::string> targets;
  std::map<std::string, int> bb_to_dis;
  std::vector<std::string> basic_blocks;

  if (!TargetsFile.empty()) {

    if (OutDirectory.empty()) {
      FATAL("Provide output directory '-outdir <directory>'");
      return PreservedAnalyses::none();
    }

    std::ifstream targetsfile(TargetsFile);
    std::string line;
    while (std::getline(targetsfile, line))
      targets.push_back(line);
    targetsfile.close();

    is_aflgo_preprocessing = true;

  } else if (!DistanceFile.empty()) {

    std::ifstream cf(DistanceFile);
    if (cf.is_open()) {

      std::string line;
      while (getline(cf, line)) {

        std::size_t pos = line.find(",");
        std::string bb_name = line.substr(0, pos);
        int bb_dis = (int) (100.0 * atof(line.substr(pos + 1, line.length()).c_str()));

        bb_to_dis.emplace(bb_name, bb_dis);
        basic_blocks.push_back(bb_name);

      }
      cf.close();

      is_aflgo = true;

    } else {
      FATAL("Unable to find %s.", DistanceFile.c_str());
      return PreservedAnalyses::none();
    }

  }

  /* Show a banner */

  char be_quiet = 0;

  if (isatty(2) && !getenv("AFL_QUIET")) {

    if (is_aflgo || is_aflgo_preprocessing)
      SAYF(cCYA "aflgo-llvm-pass (yeah!) " cBRI VERSION cRST " (%s mode)\n",
           (is_aflgo_preprocessing ? "preprocessing" : "distance instrumentation"));
    else
      SAYF(cCYA "afl-llvm-pass " cBRI VERSION cRST " by <lszekeres@google.com>\n");


  } else be_quiet = 1;

  // /* Decide instrumentation ratio */

  char* inst_ratio_str = getenv("AFL_INST_RATIO");
  unsigned int inst_ratio = 100;

  if (inst_ratio_str) {

    if (sscanf(inst_ratio_str, "%u", &inst_ratio) != 1 || !inst_ratio ||
        inst_ratio > 100)
      FATAL("Bad value of AFL_INST_RATIO (must be between 1 and 100)");

  }

  // /* Default: Not selective */
  char* is_selective_str = getenv("AFLGO_SELECTIVE");
  unsigned int is_selective = 0;

  if (is_selective_str && sscanf(is_selective_str, "%u", &is_selective) != 1)
    FATAL("Bad value of AFLGO_SELECTIVE (must be 0 or 1)");

  char* dinst_ratio_str = getenv("AFLGO_INST_RATIO");
  unsigned int dinst_ratio = 100;

  if (dinst_ratio_str) {

    if (sscanf(dinst_ratio_str, "%u", &dinst_ratio) != 1 || !dinst_ratio ||
        dinst_ratio > 100)
      FATAL("Bad value of AFLGO_INST_RATIO (must be between 1 and 100)");

  }

  /* Instrument all the things! */

  // /* ydy */
  // for (auto &F: M) {
  //   llvm::errs() << "each function name : " <<F.getName().str(); 
  //   for (auto &BB : F) {
  //     llvm::errs() << "each BB name : " << BB.getName().str();
  //     for (auto &I : BB) {
  //       llvm::errs() << "each instruction name : " << I.getName().str();
  //     }
  //   }
  // }
  // /* ydy */

  int inst_blocks = 0;

  if (is_aflgo_preprocessing) {

    std::ofstream bbnames(OutDirectory + "/BBnames.txt", std::ofstream::out | std::ofstream::app);
    std::ofstream bbcalls(OutDirectory + "/BBcalls.txt", std::ofstream::out | std::ofstream::app);
    std::ofstream fnames(OutDirectory + "/Fnames.txt", std::ofstream::out | std::ofstream::app);
    std::ofstream ftargets(OutDirectory + "/Ftargets.txt", std::ofstream::out | std::ofstream::app);

    /* Create dot-files directory */
    std::string dotfiles(OutDirectory + "/dot-files");
    if (sys::fs::create_directory(dotfiles)) {
      FATAL("Could not create directory %s.", dotfiles.c_str());
    }

    /*###############################*/
    /*####  funcName check  ######*/
    /*###############################*/
    
    for (auto &F : M) {

      bool has_BBs = false;
      std::string funcName = F.getName().str();
      

      /* Black list of function names */
      if (isBlacklisted(&F)) {
        continue;
      }

      bool is_target = false;
      for (auto &BB : F) {

        std::string bb_name("");
        std::string filename;
        unsigned line;
        

        for (auto &I : BB) {
          getDebugLoc(&I, filename, line);

          /* Don't worry about external libs */
          static const std::string Xlibs("/usr/");
          if (filename.empty() || line == 0 || !filename.compare(0, Xlibs.size(), Xlibs))
            continue;
          // /* ydy find BB code */
          // /* ------------------------ */
          // if (filename != "main.rs")
          //     continue;

          // /* ------------------------ */

          if (bb_name.empty()) {
            std::size_t found = filename.find_last_of("/\\");
            if (found != std::string::npos)
              filename = filename.substr(found + 1);

            bb_name = filename + ":" + std::to_string(line);
          }


          if (!is_target) {
              for (auto &target : targets) {
                std::size_t found = target.find_last_of("/\\");
                if (found != std::string::npos)
                  target = target.substr(found + 1);

                std::size_t pos = target.find_last_of(":");
                std::string target_file = target.substr(0, pos);
                unsigned int target_line = atoi(target.substr(pos + 1).c_str());

                if (!target_file.compare(filename) && target_line == line)
                  is_target = true;

              }
            }

            if (auto *c = dyn_cast<CallInst>(&I)) {

              std::size_t found = filename.find_last_of("/\\");
              if (found != std::string::npos)
                filename = filename.substr(found + 1);

              if (auto *CalledF = c->getCalledFunction()) {
                if (!isBlacklisted(CalledF))
                  bbcalls << bb_name << "," << CalledF->getName().str() << "\n";
              }
            }
        }

        if (!bb_name.empty()) {
          BB.setName(bb_name + ":");
          llvm::errs() << "before BB setname : "<< bb_name << "\n\n";
          if (!BB.hasName()) {
            std::string newname = bb_name + ":";
            Twine t(newname);
            SmallString<256> NameData;
            StringRef NameRef = t.toStringRef(NameData);
            MallocAllocator Allocator;
            BB.setValueName(ValueName::Create(NameRef, Allocator));
          }
          
          llvm::errs() << "after BB setname : " << BB.getName().str() << "\n\n";
          bbnames << BB.getName().str() << "\n";
          has_BBs = true;

#ifdef AFLGO_TRACING
          auto *TI = BB.getTerminator();
          IRBuilder<> Builder(TI);

          Value *bbnameVal = Builder.CreateGlobalStringPtr(bb_name);
          Type *Args[] = {
              Type::getInt8PtrTy(M.getContext()) //uint8_t* bb_name
          };
          FunctionType *FTy = FunctionType::get(Type::getVoidTy(M.getContext()), Args, false);
          Constant *instrumented = M.getOrInsertFunction("llvm_profiling_call", FTy);
          Builder.CreateCall(instrumented, {bbnameVal});
#endif

        }
      }

      if (has_BBs) {
        /* Print CFG */
        std::string cfgFileName = dotfiles + "/cfg." + funcName + ".dot";
        std::error_code EC;
        raw_fd_ostream cfgFile(cfgFileName, EC, sys::fs::OF_None);
        if (!EC) {
          WriteGraph(cfgFile, &F, true);
        }

        if (is_target)
          ftargets << F.getName().str() << "\n";
        fnames << F.getName().str() << "\n";
      }
    }

  } else {
    /* Distance instrumentation */
    
    LLVMContext &C = M.getContext();
    IntegerType *Int8Ty  = IntegerType::getInt8Ty(C);
    IntegerType *Int32Ty = IntegerType::getInt32Ty(C);
    IntegerType *Int64Ty = IntegerType::getInt64Ty(C);

#ifdef __x86_64__
    IntegerType *LargestType = Int64Ty;
    ConstantInt *MapCntLoc = ConstantInt::get(LargestType, MAP_SIZE + 8);
#else
    IntegerType *LargestType = Int32Ty;
    ConstantInt *MapCntLoc = ConstantInt::get(LargestType, MAP_SIZE + 4);
#endif
    ConstantInt *MapDistLoc = ConstantInt::get(LargestType, MAP_SIZE);
    ConstantInt *One = ConstantInt::get(LargestType, 1);

    /* Get globals for the SHM region and the previous location. Note that
       __afl_prev_loc is thread-local. */

    GlobalVariable *AFLMapPtr =
        new GlobalVariable(M, PointerType::get(Int8Ty, 0), false,
                           GlobalValue::ExternalLinkage, 0, "__afl_area_ptr");

    GlobalVariable *AFLPrevLoc = new GlobalVariable(
        M, Int32Ty, false, GlobalValue::ExternalLinkage, 0, "__afl_prev_loc",
        0, GlobalVariable::GeneralDynamicTLSModel, 0, false);
   
    for (auto &F : M) {
      int distance = -1;

      for (auto &BB : F) {
        distance = -1;

        if (is_aflgo) {
          // llvm::errs() << "is_aflgo works" << "\n\n\n";
          std::string bb_name;
          for (auto &I : BB) {
            std::string filename;
            unsigned line;
            getDebugLoc(&I, filename, line);

            // llvm::errs() << "getDebugLoc func done" << "\n\n";


            if (filename.empty() || line == 0)
              continue;

            // llvm::errs() << "is_aflgo filename : " << filename << " Line : " << line << "\n\n";
            std::size_t found = filename.find_last_of("/\\");
            if (found != std::string::npos)
              filename = filename.substr(found + 1);
            
            bb_name = filename + ":" + std::to_string(line);
            // llvm::errs() << "basic block name is "<< bb_name << "\n\n"; 
            break;
          }

          if (!bb_name.empty()) {
            // llvm::errs() << "bb_name is not empty" << "\n\n\n";
            if (find(basic_blocks.begin(), basic_blocks.end(), bb_name) == basic_blocks.end()) {
              // llvm::errs() << "bb_name & distance value not exists!!!\n\n";
              if (is_selective)
                // llvm::errs() << "not exists and also is_selective\n\n";
                continue;

            } else {

              /* Find distance for BB */

              if (AFL_R(100) < dinst_ratio) {
                std::map<std::string,int>::iterator it;
                for (it = bb_to_dis.begin(); it != bb_to_dis.end(); ++it)
                  if (it->first.compare(bb_name) == 0)
                    distance = it->second;
                    // llvm::errs() << "Find distance for BB - The distance value is : " << distance << "\n\n\n";

              }
            }
          }
        }


        BasicBlock::iterator IP = BB.getFirstInsertionPt();
        IRBuilder<> IRB(&(*IP));

        if (AFL_R(100) >= inst_ratio) continue;

        /* Make up cur_loc */

        unsigned int cur_loc = AFL_R(MAP_SIZE);

        ConstantInt *CurLoc = ConstantInt::get(Int32Ty, cur_loc);

        /* ##################original prev_loc code######################### */
        // /* Load prev_loc */

        // LoadInst *PrevLoc = IRB.CreateLoad(AFLPrevLoc);
        // PrevLoc->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
        // Value *PrevLocCasted = IRB.CreateZExt(PrevLoc, IRB.getInt32Ty());
        /* ##################original prev_loc code######################### */


        /* ##################ydy prev_loc code######################### */
        /* Load prev_loc */

        LoadInst *PrevLoc = IRB.CreateLoad(IRB.getInt32Ty(), AFLPrevLoc);
        PrevLoc->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
        Value *PrevLocCasted = IRB.CreateZExt(PrevLoc, IRB.getInt32Ty());
        /* ##################ydy prev_loc code######################### */
      

        /* ##################original SHM code######################### */
        // /* Load SHM pointer */

        // LoadInst *MapPtr = IRB.CreateLoad(AFLMapPtr);
        // MapPtr->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
        // Value *MapPtrIdx =
        //     IRB.CreateGEP(MapPtr, IRB.CreateXor(PrevLocCasted, CurLoc));
        /* ##################original SHM code######################### */


        /* ##################ydy SHM code######################### */
        /* Load SHM pointer */

        LoadInst *MapPtr = IRB.CreateLoad(PointerType::get(Int8Ty, 0), AFLMapPtr);
        MapPtr->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
        Value *MapPtrIdx =
            IRB.CreateGEP(Int8Ty, MapPtr, IRB.CreateXor(PrevLocCasted, CurLoc));
        /* ##################ydy SHM code######################### */


        /* ##################original bitmap code######################### */
        // /* Update bitmap */

        // LoadInst *Counter = IRB.CreateLoad(MapPtrIdx);
        // Counter->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
        // Value *Incr = IRB.CreateAdd(Counter, ConstantInt::get(Int8Ty, 1));
        // IRB.CreateStore(Incr, MapPtrIdx)
        //    ->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
        /* ##################original bitmap code######################### */
        

        /* ##################ydy bitmap code######################### */
        /* Update bitmap */

        LoadInst *Counter = IRB.CreateLoad(IRB.getInt8Ty(), MapPtrIdx);
        Counter->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
        Value *Incr = IRB.CreateAdd(Counter, ConstantInt::get(Int8Ty, 1));
        IRB.CreateStore(Incr, MapPtrIdx)
           ->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
        /* ##################ydy bitmap code######################### */


        /* ##################original prev_loc to cur_loc code######################### */
        // /* Set prev_loc to cur_loc >> 1 */

        // StoreInst *Store =
        //     IRB.CreateStore(ConstantInt::get(Int32Ty, cur_loc >> 1), AFLPrevLoc);
        // Store->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
        /* ##################original prev_loc to cur_loc code######################### */


        /* ##################ydy prev_loc to cur_loc code######################### */
        /* Set prev_loc to cur_loc >> 1 */

        StoreInst *Store =
            IRB.CreateStore(ConstantInt::get(Int32Ty, cur_loc >> 1), AFLPrevLoc);
        Store->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
        /* ##################ydy prev_loc to cur_loc code######################### */


        if (distance >= 0) {

          /* ##################original Distance code######################### */
          // ConstantInt *Distance =
          //     ConstantInt::get(LargestType, (unsigned) distance);
          /* ##################original Distance code######################### */

          /* ##################ydy Distance code######################### */
          ConstantInt *Distance =
              ConstantInt::get(LargestType, (unsigned) distance);
          llvm::errs() << "Distance Pass \n\n";
          /* ##################ydy Distance code######################### */
          

          /* ##################original Add distance code######################### */
          // /* Add distance to shm[MAPSIZE] */
          // Value *MapDistPtr = IRB.CreateBitCast(
          //     IRB.CreateGEP(MapPtr, MapDistLoc), LargestType->getPointerTo());
          // LoadInst *MapDist = IRB.CreateLoad(MapDistPtr);
          // MapDist->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
          /* ##################original Add distance code######################### */


          /* ##################ydy Add distance code######################### */
          /* Add distance to shm[MAPSIZE] */
          Value *MapDistPtr = IRB.CreateBitCast(
              IRB.CreateGEP(Int8Ty, MapPtr, MapDistLoc), LargestType->getPointerTo());
          llvm::errs() << "MapDistPtr Pass \n\n";
          // LoadInst *MapDist = IRB.CreateLoad(IRB.getInt64Ty(), MapDistPtr);
          // llvm::errs() << "IRB.getInt64Ty() Pass \n\n";
          LoadInst *MapDist = IRB.CreateLoad(LargestType, MapDistPtr);
          llvm::errs() << "LargestType Pass \n\n";
          MapDist->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
          llvm::errs() << "MapDist Pass \n\n";
          /* ##################ydy Add distance code######################### */


          /* ##################original IncrDist code######################### */
          // Value *IncrDist = IRB.CreateAdd(MapDist, Distance);
          // IRB.CreateStore(IncrDist, MapDistPtr)
          //     ->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
          /* ##################original IncrDist code######################### */

          /* ##################ydy IncrDist code######################### */
          Value *IncrDist = IRB.CreateAdd(MapDist, Distance);
          IRB.CreateStore(IncrDist, MapDistPtr)
              ->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
          llvm::errs() << "IncrDist Pass \n\n";
          /* ##################ydy IncrDist code######################### */


          /* ##################original increase count code######################### */
          /* Increase count at shm[MAPSIZE + (4 or 8)] */
          // Value *MapCntPtr = IRB.CreateBitCast(
          //     IRB.CreateGEP(MapPtr, MapCntLoc), LargestType->getPointerTo());
          // LoadInst *MapCnt = IRB.CreateLoad(MapCntPtr);
          // MapCnt->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));

          // Value *IncrCnt = IRB.CreateAdd(MapCnt, One);
          // IRB.CreateStore(IncrCnt, MapCntPtr)
          //     ->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
          /* ##################original increase count code######################### */



          /* ##################ydy increase count code######################### */
          /* Increase count at shm[MAPSIZE + (4 or 8)] */
              ConstantInt *MapCntLoc = ConstantInt::get(LargestType, MAP_SIZE + 8);
              ConstantInt *MapDistLoc = ConstantInt::get(LargestType, MAP_SIZE);

          Value *MapCntPtr = IRB.CreateBitCast(
              IRB.CreateGEP(Int8Ty, MapPtr, MapCntLoc), LargestType->getPointerTo());
          llvm::errs() << "MapCntPtr Pass \n\n";
          LoadInst *MapCnt = IRB.CreateLoad(LargestType, MapCntPtr);
          MapCnt->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
          llvm::errs() << "MapCnt Pass \n\n";

          Value *IncrCnt = IRB.CreateAdd(MapCnt, One);
          llvm::errs() << "IncrCnt Pass \n\n";
          IRB.CreateStore(IncrCnt, MapCntPtr)
              ->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
          llvm::errs() << "CreateStore Pass \n\n";
          /* ##################ydy increase count code######################### */
          
        }

        inst_blocks++;

      }
    }
  }

  /* Say something nice. */
  
  if (!is_aflgo_preprocessing && !be_quiet) {

    if (!inst_blocks) WARNF("No instrumentation targets found.");
    else OKF("Instrumented %u locations (%s mode, ratio %u%%, dist. ratio %u%%).",
             inst_blocks,
             getenv("AFL_HARDEN")
             ? "hardened"
             : ((getenv("AFL_USE_ASAN") || getenv("AFL_USE_MSAN"))
               ? "ASAN/MSAN" : "non-hardened"),
             inst_ratio, dinst_ratio);

  }


  /*#############################*/
  /*##  SanitiizerCoverage-run ##*/
  /*#############################*/
  // ModuleSanitizerCoverage ModuleSancov(Options, Allowlist.get(),
  //                                      Blocklist.get());
  // auto &FAM = MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  // auto DTCallback = [&FAM](Function &F) -> const DominatorTree * {
  //   return &FAM.getResult<DominatorTreeAnalysis>(F);
  // };
  // auto PDTCallback = [&FAM](Function &F) -> const PostDominatorTree * {
  //   return &FAM.getResult<PostDominatorTreeAnalysis>(F);
  // };
  // if (ModuleSancov.instrumentModule(M, DTCallback, PDTCallback))
  //   return PreservedAnalyses::none();
  // return PreservedAnalyses::all();

  return PreservedAnalyses::all();
}

/*#############################*/
/*#############################*/
/*#############################*/
/*#############################*/
/* ydy code ends */
/*#############################*/
/*#############################*/
/*#############################*/
/*#############################*/

std::pair<Value *, Value *>
ModuleSanitizerCoverage::CreateSecStartEnd(Module &M, const char *Section,
                                           Type *Ty) {
  // Use ExternalWeak so that if all sections are discarded due to section
  // garbage collection, the linker will not report undefined symbol errors.
  // Windows defines the start/stop symbols in compiler-rt so no need for
  // ExternalWeak.
  GlobalValue::LinkageTypes Linkage = TargetTriple.isOSBinFormatCOFF()
                                          ? GlobalVariable::ExternalLinkage
                                          : GlobalVariable::ExternalWeakLinkage;
  GlobalVariable *SecStart =
      new GlobalVariable(M, Ty, false, Linkage, nullptr,
                         getSectionStart(Section));
  SecStart->setVisibility(GlobalValue::HiddenVisibility);
  GlobalVariable *SecEnd =
      new GlobalVariable(M, Ty, false, Linkage, nullptr,
                         getSectionEnd(Section));
  SecEnd->setVisibility(GlobalValue::HiddenVisibility);
  IRBuilder<> IRB(M.getContext());
  if (!TargetTriple.isOSBinFormatCOFF())
    return std::make_pair(SecStart, SecEnd);

  // Account for the fact that on windows-msvc __start_* symbols actually
  // point to a uint64_t before the start of the array.
  auto SecStartI8Ptr = IRB.CreatePointerCast(SecStart, Int8PtrTy);
  auto GEP = IRB.CreateGEP(Int8Ty, SecStartI8Ptr,
                           ConstantInt::get(IntptrTy, sizeof(uint64_t)));
  return std::make_pair(IRB.CreatePointerCast(GEP, PointerType::getUnqual(Ty)),
                        SecEnd);
}

Function *ModuleSanitizerCoverage::CreateInitCallsForSections(
    Module &M, const char *CtorName, const char *InitFunctionName, Type *Ty,
    const char *Section) {
  auto SecStartEnd = CreateSecStartEnd(M, Section, Ty);
  auto SecStart = SecStartEnd.first;
  auto SecEnd = SecStartEnd.second;
  Function *CtorFunc;
  Type *PtrTy = PointerType::getUnqual(Ty);
  std::tie(CtorFunc, std::ignore) = createSanitizerCtorAndInitFunctions(
      M, CtorName, InitFunctionName, {PtrTy, PtrTy}, {SecStart, SecEnd});
  assert(CtorFunc->getName() == CtorName);

  if (TargetTriple.supportsCOMDAT()) {
    // Use comdat to dedup CtorFunc.
    CtorFunc->setComdat(M.getOrInsertComdat(CtorName));
    appendToGlobalCtors(M, CtorFunc, SanCtorAndDtorPriority, CtorFunc);
  } else {
    appendToGlobalCtors(M, CtorFunc, SanCtorAndDtorPriority);
  }

  if (TargetTriple.isOSBinFormatCOFF()) {
    // In COFF files, if the contructors are set as COMDAT (they are because
    // COFF supports COMDAT) and the linker flag /OPT:REF (strip unreferenced
    // functions and data) is used, the constructors get stripped. To prevent
    // this, give the constructors weak ODR linkage and ensure the linker knows
    // to include the sancov constructor. This way the linker can deduplicate
    // the constructors but always leave one copy.
    CtorFunc->setLinkage(GlobalValue::WeakODRLinkage);
  }
  return CtorFunc;
}

bool ModuleSanitizerCoverage::instrumentModule(
    Module &M, DomTreeCallback DTCallback, PostDomTreeCallback PDTCallback) {
  if (Options.CoverageType == SanitizerCoverageOptions::SCK_None)
    return false;
  if (Allowlist &&
      !Allowlist->inSection("coverage", "src", M.getSourceFileName()))
    return false;
  if (Blocklist &&
      Blocklist->inSection("coverage", "src", M.getSourceFileName()))
    return false;
  C = &(M.getContext());
  DL = &M.getDataLayout();
  CurModule = &M;
  CurModuleUniqueId = getUniqueModuleId(CurModule);
  TargetTriple = Triple(M.getTargetTriple());
  FunctionGuardArray = nullptr;
  Function8bitCounterArray = nullptr;
  FunctionBoolArray = nullptr;
  FunctionPCsArray = nullptr;
  IntptrTy = Type::getIntNTy(*C, DL->getPointerSizeInBits());
  IntptrPtrTy = PointerType::getUnqual(IntptrTy);
  Type *VoidTy = Type::getVoidTy(*C);
  IRBuilder<> IRB(*C);
  Int128PtrTy = PointerType::getUnqual(IRB.getInt128Ty());
  Int64PtrTy = PointerType::getUnqual(IRB.getInt64Ty());
  Int16PtrTy = PointerType::getUnqual(IRB.getInt16Ty());
  Int32PtrTy = PointerType::getUnqual(IRB.getInt32Ty());
  Int8PtrTy = PointerType::getUnqual(IRB.getInt8Ty());
  Int1PtrTy = PointerType::getUnqual(IRB.getInt1Ty());
  Int64Ty = IRB.getInt64Ty();
  Int32Ty = IRB.getInt32Ty();
  Int16Ty = IRB.getInt16Ty();
  Int8Ty = IRB.getInt8Ty();
  Int1Ty = IRB.getInt1Ty();

  SanCovTracePCIndir =
      M.getOrInsertFunction(SanCovTracePCIndirName, VoidTy, IntptrTy);
  // Make sure smaller parameters are zero-extended to i64 if required by the
  // target ABI.
  AttributeList SanCovTraceCmpZeroExtAL;
  SanCovTraceCmpZeroExtAL =
      SanCovTraceCmpZeroExtAL.addParamAttribute(*C, 0, Attribute::ZExt);
  SanCovTraceCmpZeroExtAL =
      SanCovTraceCmpZeroExtAL.addParamAttribute(*C, 1, Attribute::ZExt);

  SanCovTraceCmpFunction[0] =
      M.getOrInsertFunction(SanCovTraceCmp1, SanCovTraceCmpZeroExtAL, VoidTy,
                            IRB.getInt8Ty(), IRB.getInt8Ty());
  SanCovTraceCmpFunction[1] =
      M.getOrInsertFunction(SanCovTraceCmp2, SanCovTraceCmpZeroExtAL, VoidTy,
                            IRB.getInt16Ty(), IRB.getInt16Ty());
  SanCovTraceCmpFunction[2] =
      M.getOrInsertFunction(SanCovTraceCmp4, SanCovTraceCmpZeroExtAL, VoidTy,
                            IRB.getInt32Ty(), IRB.getInt32Ty());
  SanCovTraceCmpFunction[3] =
      M.getOrInsertFunction(SanCovTraceCmp8, VoidTy, Int64Ty, Int64Ty);

  SanCovTraceConstCmpFunction[0] = M.getOrInsertFunction(
      SanCovTraceConstCmp1, SanCovTraceCmpZeroExtAL, VoidTy, Int8Ty, Int8Ty);
  SanCovTraceConstCmpFunction[1] = M.getOrInsertFunction(
      SanCovTraceConstCmp2, SanCovTraceCmpZeroExtAL, VoidTy, Int16Ty, Int16Ty);
  SanCovTraceConstCmpFunction[2] = M.getOrInsertFunction(
      SanCovTraceConstCmp4, SanCovTraceCmpZeroExtAL, VoidTy, Int32Ty, Int32Ty);
  SanCovTraceConstCmpFunction[3] =
      M.getOrInsertFunction(SanCovTraceConstCmp8, VoidTy, Int64Ty, Int64Ty);

  // Loads.
  SanCovLoadFunction[0] = M.getOrInsertFunction(SanCovLoad1, VoidTy, Int8PtrTy);
  SanCovLoadFunction[1] =
      M.getOrInsertFunction(SanCovLoad2, VoidTy, Int16PtrTy);
  SanCovLoadFunction[2] =
      M.getOrInsertFunction(SanCovLoad4, VoidTy, Int32PtrTy);
  SanCovLoadFunction[3] =
      M.getOrInsertFunction(SanCovLoad8, VoidTy, Int64PtrTy);
  SanCovLoadFunction[4] =
      M.getOrInsertFunction(SanCovLoad16, VoidTy, Int128PtrTy);
  // Stores.
  SanCovStoreFunction[0] =
      M.getOrInsertFunction(SanCovStore1, VoidTy, Int8PtrTy);
  SanCovStoreFunction[1] =
      M.getOrInsertFunction(SanCovStore2, VoidTy, Int16PtrTy);
  SanCovStoreFunction[2] =
      M.getOrInsertFunction(SanCovStore4, VoidTy, Int32PtrTy);
  SanCovStoreFunction[3] =
      M.getOrInsertFunction(SanCovStore8, VoidTy, Int64PtrTy);
  SanCovStoreFunction[4] =
      M.getOrInsertFunction(SanCovStore16, VoidTy, Int128PtrTy);

  {
    AttributeList AL;
    AL = AL.addParamAttribute(*C, 0, Attribute::ZExt);
    SanCovTraceDivFunction[0] =
        M.getOrInsertFunction(SanCovTraceDiv4, AL, VoidTy, IRB.getInt32Ty());
  }
  SanCovTraceDivFunction[1] =
      M.getOrInsertFunction(SanCovTraceDiv8, VoidTy, Int64Ty);
  SanCovTraceGepFunction =
      M.getOrInsertFunction(SanCovTraceGep, VoidTy, IntptrTy);
  SanCovTraceSwitchFunction =
      M.getOrInsertFunction(SanCovTraceSwitchName, VoidTy, Int64Ty, Int64PtrTy);

  Constant *SanCovLowestStackConstant =
      M.getOrInsertGlobal(SanCovLowestStackName, IntptrTy);
  SanCovLowestStack = dyn_cast<GlobalVariable>(SanCovLowestStackConstant);
  if (!SanCovLowestStack || SanCovLowestStack->getValueType() != IntptrTy) {
    C->emitError(StringRef("'") + SanCovLowestStackName +
                 "' should not be declared by the user");
    return true;
  }
  SanCovLowestStack->setThreadLocalMode(
      GlobalValue::ThreadLocalMode::InitialExecTLSModel);
  if (Options.StackDepth && !SanCovLowestStack->isDeclaration())
    SanCovLowestStack->setInitializer(Constant::getAllOnesValue(IntptrTy));

  SanCovTracePC = M.getOrInsertFunction(SanCovTracePCName, VoidTy);
  SanCovTracePCGuard =
      M.getOrInsertFunction(SanCovTracePCGuardName, VoidTy, Int32PtrTy);

  for (auto &F : M)
    instrumentFunction(F, DTCallback, PDTCallback);

  Function *Ctor = nullptr;

  if (FunctionGuardArray)
    Ctor = CreateInitCallsForSections(M, SanCovModuleCtorTracePcGuardName,
                                      SanCovTracePCGuardInitName, Int32Ty,
                                      SanCovGuardsSectionName);
  if (Function8bitCounterArray)
    Ctor = CreateInitCallsForSections(M, SanCovModuleCtor8bitCountersName,
                                      SanCov8bitCountersInitName, Int8Ty,
                                      SanCovCountersSectionName);
  if (FunctionBoolArray) {
    Ctor = CreateInitCallsForSections(M, SanCovModuleCtorBoolFlagName,
                                      SanCovBoolFlagInitName, Int1Ty,
                                      SanCovBoolFlagSectionName);
  }
  if (Ctor && Options.PCTable) {
    auto SecStartEnd = CreateSecStartEnd(M, SanCovPCsSectionName, IntptrTy);
    FunctionCallee InitFunction = declareSanitizerInitFunction(
        M, SanCovPCsInitName, {IntptrPtrTy, IntptrPtrTy});
    IRBuilder<> IRBCtor(Ctor->getEntryBlock().getTerminator());
    IRBCtor.CreateCall(InitFunction, {SecStartEnd.first, SecStartEnd.second});
  }
  appendToUsed(M, GlobalsToAppendToUsed);
  appendToCompilerUsed(M, GlobalsToAppendToCompilerUsed);
  return true;
}

// True if block has successors and it dominates all of them.
static bool isFullDominator(const BasicBlock *BB, const DominatorTree *DT) {
  if (succ_empty(BB))
    return false;

  return llvm::all_of(successors(BB), [&](const BasicBlock *SUCC) {
    return DT->dominates(BB, SUCC);
  });
}

// True if block has predecessors and it postdominates all of them.
static bool isFullPostDominator(const BasicBlock *BB,
                                const PostDominatorTree *PDT) {
  if (pred_empty(BB))
    return false;

  return llvm::all_of(predecessors(BB), [&](const BasicBlock *PRED) {
    return PDT->dominates(BB, PRED);
  });
}

static bool shouldInstrumentBlock(const Function &F, const BasicBlock *BB,
                                  const DominatorTree *DT,
                                  const PostDominatorTree *PDT,
                                  const SanitizerCoverageOptions &Options) {
  // Don't insert coverage for blocks containing nothing but unreachable: we
  // will never call __sanitizer_cov() for them, so counting them in
  // NumberOfInstrumentedBlocks() might complicate calculation of code coverage
  // percentage. Also, unreachable instructions frequently have no debug
  // locations.
  if (isa<UnreachableInst>(BB->getFirstNonPHIOrDbgOrLifetime()))
    return false;

  // Don't insert coverage into blocks without a valid insertion point
  // (catchswitch blocks).
  if (BB->getFirstInsertionPt() == BB->end())
    return false;

  if (Options.NoPrune || &F.getEntryBlock() == BB)
    return true;

  if (Options.CoverageType == SanitizerCoverageOptions::SCK_Function &&
      &F.getEntryBlock() != BB)
    return false;

  // Do not instrument full dominators, or full post-dominators with multiple
  // predecessors.
  return !isFullDominator(BB, DT)
    && !(isFullPostDominator(BB, PDT) && !BB->getSinglePredecessor());
}


// Returns true iff From->To is a backedge.
// A twist here is that we treat From->To as a backedge if
//   * To dominates From or
//   * To->UniqueSuccessor dominates From
static bool IsBackEdge(BasicBlock *From, BasicBlock *To,
                       const DominatorTree *DT) {
  if (DT->dominates(To, From))
    return true;
  if (auto Next = To->getUniqueSuccessor())
    if (DT->dominates(Next, From))
      return true;
  return false;
}

// Prunes uninteresting Cmp instrumentation:
//   * CMP instructions that feed into loop backedge branch.
//
// Note that Cmp pruning is controlled by the same flag as the
// BB pruning.
static bool IsInterestingCmp(ICmpInst *CMP, const DominatorTree *DT,
                             const SanitizerCoverageOptions &Options) {
  if (!Options.NoPrune)
    if (CMP->hasOneUse())
      if (auto BR = dyn_cast<BranchInst>(CMP->user_back()))
        for (BasicBlock *B : BR->successors())
          if (IsBackEdge(BR->getParent(), B, DT))
            return false;
  return true;
}

void ModuleSanitizerCoverage::instrumentFunction(
    Function &F, DomTreeCallback DTCallback, PostDomTreeCallback PDTCallback) {
  if (F.empty())
    return;
  if (F.getName().find(".module_ctor") != std::string::npos)
    return; // Should not instrument sanitizer init functions.
  if (F.getName().startswith("__sanitizer_"))
    return; // Don't instrument __sanitizer_* callbacks.
  // Don't touch available_externally functions, their actual body is elewhere.
  if (F.getLinkage() == GlobalValue::AvailableExternallyLinkage)
    return;
  // Don't instrument MSVC CRT configuration helpers. They may run before normal
  // initialization.
  if (F.getName() == "__local_stdio_printf_options" ||
      F.getName() == "__local_stdio_scanf_options")
    return;
  if (isa<UnreachableInst>(F.getEntryBlock().getTerminator()))
    return;
  // Don't instrument functions using SEH for now. Splitting basic blocks like
  // we do for coverage breaks WinEHPrepare.
  // FIXME: Remove this when SEH no longer uses landingpad pattern matching.
  if (F.hasPersonalityFn() &&
      isAsynchronousEHPersonality(classifyEHPersonality(F.getPersonalityFn())))
    return;
  if (Allowlist && !Allowlist->inSection("coverage", "fun", F.getName()))
    return;
  if (Blocklist && Blocklist->inSection("coverage", "fun", F.getName()))
    return;
  if (F.hasFnAttribute(Attribute::NoSanitizeCoverage))
    return;
  if (Options.CoverageType >= SanitizerCoverageOptions::SCK_Edge)
    SplitAllCriticalEdges(F, CriticalEdgeSplittingOptions().setIgnoreUnreachableDests());
  SmallVector<Instruction *, 8> IndirCalls;
  SmallVector<BasicBlock *, 16> BlocksToInstrument;
  SmallVector<Instruction *, 8> CmpTraceTargets;
  SmallVector<Instruction *, 8> SwitchTraceTargets;
  SmallVector<BinaryOperator *, 8> DivTraceTargets;
  SmallVector<GetElementPtrInst *, 8> GepTraceTargets;
  SmallVector<LoadInst *, 8> Loads;
  SmallVector<StoreInst *, 8> Stores;

  const DominatorTree *DT = DTCallback(F);
  const PostDominatorTree *PDT = PDTCallback(F);
  bool IsLeafFunc = true;

  for (auto &BB : F) {
    if (shouldInstrumentBlock(F, &BB, DT, PDT, Options))
      BlocksToInstrument.push_back(&BB);
    for (auto &Inst : BB) {
      if (Options.IndirectCalls) {
        CallBase *CB = dyn_cast<CallBase>(&Inst);
        if (CB && !CB->getCalledFunction())
          IndirCalls.push_back(&Inst);
      }
      if (Options.TraceCmp) {
        if (ICmpInst *CMP = dyn_cast<ICmpInst>(&Inst))
          if (IsInterestingCmp(CMP, DT, Options))
            CmpTraceTargets.push_back(&Inst);
        if (isa<SwitchInst>(&Inst))
          SwitchTraceTargets.push_back(&Inst);
      }
      if (Options.TraceDiv)
        if (BinaryOperator *BO = dyn_cast<BinaryOperator>(&Inst))
          if (BO->getOpcode() == Instruction::SDiv ||
              BO->getOpcode() == Instruction::UDiv)
            DivTraceTargets.push_back(BO);
      if (Options.TraceGep)
        if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(&Inst))
          GepTraceTargets.push_back(GEP);
      if (Options.TraceLoads)
        if (LoadInst *LI = dyn_cast<LoadInst>(&Inst))
          Loads.push_back(LI);
      if (Options.TraceStores)
        if (StoreInst *SI = dyn_cast<StoreInst>(&Inst))
          Stores.push_back(SI);
      if (Options.StackDepth)
        if (isa<InvokeInst>(Inst) ||
            (isa<CallInst>(Inst) && !isa<IntrinsicInst>(Inst)))
          IsLeafFunc = false;
    }
  }

  InjectCoverage(F, BlocksToInstrument, IsLeafFunc);
  InjectCoverageForIndirectCalls(F, IndirCalls);
  InjectTraceForCmp(F, CmpTraceTargets);
  InjectTraceForSwitch(F, SwitchTraceTargets);
  InjectTraceForDiv(F, DivTraceTargets);
  InjectTraceForGep(F, GepTraceTargets);
  InjectTraceForLoadsAndStores(F, Loads, Stores);
}

GlobalVariable *ModuleSanitizerCoverage::CreateFunctionLocalArrayInSection(
    size_t NumElements, Function &F, Type *Ty, const char *Section) {
  ArrayType *ArrayTy = ArrayType::get(Ty, NumElements);
  auto Array = new GlobalVariable(
      *CurModule, ArrayTy, false, GlobalVariable::PrivateLinkage,
      Constant::getNullValue(ArrayTy), "__sancov_gen_");

  if (TargetTriple.supportsCOMDAT() &&
      (TargetTriple.isOSBinFormatELF() || !F.isInterposable()))
    if (auto Comdat = getOrCreateFunctionComdat(F, TargetTriple))
      Array->setComdat(Comdat);
  Array->setSection(getSectionName(Section));
  Array->setAlignment(Align(DL->getTypeStoreSize(Ty).getFixedSize()));

  // sancov_pcs parallels the other metadata section(s). Optimizers (e.g.
  // GlobalOpt/ConstantMerge) may not discard sancov_pcs and the other
  // section(s) as a unit, so we conservatively retain all unconditionally in
  // the compiler.
  //
  // With comdat (COFF/ELF), the linker can guarantee the associated sections
  // will be retained or discarded as a unit, so llvm.compiler.used is
  // sufficient. Otherwise, conservatively make all of them retained by the
  // linker.
  if (Array->hasComdat())
    GlobalsToAppendToCompilerUsed.push_back(Array);
  else
    GlobalsToAppendToUsed.push_back(Array);

  return Array;
}

GlobalVariable *
ModuleSanitizerCoverage::CreatePCArray(Function &F,
                                       ArrayRef<BasicBlock *> AllBlocks) {
  size_t N = AllBlocks.size();
  assert(N);
  SmallVector<Constant *, 32> PCs;
  IRBuilder<> IRB(&*F.getEntryBlock().getFirstInsertionPt());
  for (size_t i = 0; i < N; i++) {
    if (&F.getEntryBlock() == AllBlocks[i]) {
      PCs.push_back((Constant *)IRB.CreatePointerCast(&F, IntptrPtrTy));
      PCs.push_back((Constant *)IRB.CreateIntToPtr(
          ConstantInt::get(IntptrTy, 1), IntptrPtrTy));
    } else {
      PCs.push_back((Constant *)IRB.CreatePointerCast(
          BlockAddress::get(AllBlocks[i]), IntptrPtrTy));
      PCs.push_back((Constant *)IRB.CreateIntToPtr(
          ConstantInt::get(IntptrTy, 0), IntptrPtrTy));
    }
  }
  auto *PCArray = CreateFunctionLocalArrayInSection(N * 2, F, IntptrPtrTy,
                                                    SanCovPCsSectionName);
  PCArray->setInitializer(
      ConstantArray::get(ArrayType::get(IntptrPtrTy, N * 2), PCs));
  PCArray->setConstant(true);

  return PCArray;
}

void ModuleSanitizerCoverage::CreateFunctionLocalArrays(
    Function &F, ArrayRef<BasicBlock *> AllBlocks) {
  if (Options.TracePCGuard)
    FunctionGuardArray = CreateFunctionLocalArrayInSection(
        AllBlocks.size(), F, Int32Ty, SanCovGuardsSectionName);

  if (Options.Inline8bitCounters)
    Function8bitCounterArray = CreateFunctionLocalArrayInSection(
        AllBlocks.size(), F, Int8Ty, SanCovCountersSectionName);
  if (Options.InlineBoolFlag)
    FunctionBoolArray = CreateFunctionLocalArrayInSection(
        AllBlocks.size(), F, Int1Ty, SanCovBoolFlagSectionName);

  if (Options.PCTable)
    FunctionPCsArray = CreatePCArray(F, AllBlocks);
}

bool ModuleSanitizerCoverage::InjectCoverage(Function &F,
                                             ArrayRef<BasicBlock *> AllBlocks,
                                             bool IsLeafFunc) {
  if (AllBlocks.empty()) return false;
  CreateFunctionLocalArrays(F, AllBlocks);
  for (size_t i = 0, N = AllBlocks.size(); i < N; i++)
    InjectCoverageAtBlock(F, *AllBlocks[i], i, IsLeafFunc);
  return true;
}

// On every indirect call we call a run-time function
// __sanitizer_cov_indir_call* with two parameters:
//   - callee address,
//   - global cache array that contains CacheSize pointers (zero-initialized).
//     The cache is used to speed up recording the caller-callee pairs.
// The address of the caller is passed implicitly via caller PC.
// CacheSize is encoded in the name of the run-time function.
void ModuleSanitizerCoverage::InjectCoverageForIndirectCalls(
    Function &F, ArrayRef<Instruction *> IndirCalls) {
  if (IndirCalls.empty())
    return;
  assert(Options.TracePC || Options.TracePCGuard ||
         Options.Inline8bitCounters || Options.InlineBoolFlag);
  for (auto I : IndirCalls) {
    IRBuilder<> IRB(I);
    CallBase &CB = cast<CallBase>(*I);
    Value *Callee = CB.getCalledOperand();
    if (isa<InlineAsm>(Callee))
      continue;
    IRB.CreateCall(SanCovTracePCIndir, IRB.CreatePointerCast(Callee, IntptrTy));
  }
}

// For every switch statement we insert a call:
// __sanitizer_cov_trace_switch(CondValue,
//      {NumCases, ValueSizeInBits, Case0Value, Case1Value, Case2Value, ... })

void ModuleSanitizerCoverage::InjectTraceForSwitch(
    Function &, ArrayRef<Instruction *> SwitchTraceTargets) {
  for (auto I : SwitchTraceTargets) {
    if (SwitchInst *SI = dyn_cast<SwitchInst>(I)) {
      IRBuilder<> IRB(I);
      SmallVector<Constant *, 16> Initializers;
      Value *Cond = SI->getCondition();
      if (Cond->getType()->getScalarSizeInBits() >
          Int64Ty->getScalarSizeInBits())
        continue;
      Initializers.push_back(ConstantInt::get(Int64Ty, SI->getNumCases()));
      Initializers.push_back(
          ConstantInt::get(Int64Ty, Cond->getType()->getScalarSizeInBits()));
      if (Cond->getType()->getScalarSizeInBits() <
          Int64Ty->getScalarSizeInBits())
        Cond = IRB.CreateIntCast(Cond, Int64Ty, false);
      for (auto It : SI->cases()) {
        Constant *C = It.getCaseValue();
        if (C->getType()->getScalarSizeInBits() <
            Int64Ty->getScalarSizeInBits())
          C = ConstantExpr::getCast(CastInst::ZExt, It.getCaseValue(), Int64Ty);
        Initializers.push_back(C);
      }
      llvm::sort(drop_begin(Initializers, 2),
                 [](const Constant *A, const Constant *B) {
                   return cast<ConstantInt>(A)->getLimitedValue() <
                          cast<ConstantInt>(B)->getLimitedValue();
                 });
      ArrayType *ArrayOfInt64Ty = ArrayType::get(Int64Ty, Initializers.size());
      GlobalVariable *GV = new GlobalVariable(
          *CurModule, ArrayOfInt64Ty, false, GlobalVariable::InternalLinkage,
          ConstantArray::get(ArrayOfInt64Ty, Initializers),
          "__sancov_gen_cov_switch_values");
      IRB.CreateCall(SanCovTraceSwitchFunction,
                     {Cond, IRB.CreatePointerCast(GV, Int64PtrTy)});
    }
  }
}

void ModuleSanitizerCoverage::InjectTraceForDiv(
    Function &, ArrayRef<BinaryOperator *> DivTraceTargets) {
  for (auto BO : DivTraceTargets) {
    IRBuilder<> IRB(BO);
    Value *A1 = BO->getOperand(1);
    if (isa<ConstantInt>(A1)) continue;
    if (!A1->getType()->isIntegerTy())
      continue;
    uint64_t TypeSize = DL->getTypeStoreSizeInBits(A1->getType());
    int CallbackIdx = TypeSize == 32 ? 0 :
        TypeSize == 64 ? 1 : -1;
    if (CallbackIdx < 0) continue;
    auto Ty = Type::getIntNTy(*C, TypeSize);
    IRB.CreateCall(SanCovTraceDivFunction[CallbackIdx],
                   {IRB.CreateIntCast(A1, Ty, true)});
  }
}

void ModuleSanitizerCoverage::InjectTraceForGep(
    Function &, ArrayRef<GetElementPtrInst *> GepTraceTargets) {
  for (auto GEP : GepTraceTargets) {
    IRBuilder<> IRB(GEP);
    for (Use &Idx : GEP->indices())
      if (!isa<ConstantInt>(Idx) && Idx->getType()->isIntegerTy())
        IRB.CreateCall(SanCovTraceGepFunction,
                       {IRB.CreateIntCast(Idx, IntptrTy, true)});
  }
}

void ModuleSanitizerCoverage::InjectTraceForLoadsAndStores(
    Function &, ArrayRef<LoadInst *> Loads, ArrayRef<StoreInst *> Stores) {
  auto CallbackIdx = [&](Type *ElementTy) -> int {
    uint64_t TypeSize = DL->getTypeStoreSizeInBits(ElementTy);
    return TypeSize == 8     ? 0
           : TypeSize == 16  ? 1
           : TypeSize == 32  ? 2
           : TypeSize == 64  ? 3
           : TypeSize == 128 ? 4
                             : -1;
  };
  Type *PointerType[5] = {Int8PtrTy, Int16PtrTy, Int32PtrTy, Int64PtrTy,
                          Int128PtrTy};
  for (auto LI : Loads) {
    IRBuilder<> IRB(LI);
    auto Ptr = LI->getPointerOperand();
    int Idx = CallbackIdx(LI->getType());
    if (Idx < 0)
      continue;
    IRB.CreateCall(SanCovLoadFunction[Idx],
                   IRB.CreatePointerCast(Ptr, PointerType[Idx]));
  }
  for (auto SI : Stores) {
    IRBuilder<> IRB(SI);
    auto Ptr = SI->getPointerOperand();
    int Idx = CallbackIdx(SI->getValueOperand()->getType());
    if (Idx < 0)
      continue;
    IRB.CreateCall(SanCovStoreFunction[Idx],
                   IRB.CreatePointerCast(Ptr, PointerType[Idx]));
  }
}

void ModuleSanitizerCoverage::InjectTraceForCmp(
    Function &, ArrayRef<Instruction *> CmpTraceTargets) {
  for (auto I : CmpTraceTargets) {
    if (ICmpInst *ICMP = dyn_cast<ICmpInst>(I)) {
      IRBuilder<> IRB(ICMP);
      Value *A0 = ICMP->getOperand(0);
      Value *A1 = ICMP->getOperand(1);
      if (!A0->getType()->isIntegerTy())
        continue;
      uint64_t TypeSize = DL->getTypeStoreSizeInBits(A0->getType());
      int CallbackIdx = TypeSize == 8 ? 0 :
                        TypeSize == 16 ? 1 :
                        TypeSize == 32 ? 2 :
                        TypeSize == 64 ? 3 : -1;
      if (CallbackIdx < 0) continue;
      // __sanitizer_cov_trace_cmp((type_size << 32) | predicate, A0, A1);
      auto CallbackFunc = SanCovTraceCmpFunction[CallbackIdx];
      bool FirstIsConst = isa<ConstantInt>(A0);
      bool SecondIsConst = isa<ConstantInt>(A1);
      // If both are const, then we don't need such a comparison.
      if (FirstIsConst && SecondIsConst) continue;
      // If only one is const, then make it the first callback argument.
      if (FirstIsConst || SecondIsConst) {
        CallbackFunc = SanCovTraceConstCmpFunction[CallbackIdx];
        if (SecondIsConst)
          std::swap(A0, A1);
      }

      auto Ty = Type::getIntNTy(*C, TypeSize);
      IRB.CreateCall(CallbackFunc, {IRB.CreateIntCast(A0, Ty, true),
              IRB.CreateIntCast(A1, Ty, true)});
    }
  }
}

void ModuleSanitizerCoverage::InjectCoverageAtBlock(Function &F, BasicBlock &BB,
                                                    size_t Idx,
                                                    bool IsLeafFunc) {
  BasicBlock::iterator IP = BB.getFirstInsertionPt();
  bool IsEntryBB = &BB == &F.getEntryBlock();
  DebugLoc EntryLoc;
  if (IsEntryBB) {
    if (auto SP = F.getSubprogram())
      EntryLoc = DILocation::get(SP->getContext(), SP->getScopeLine(), 0, SP);
    // Keep static allocas and llvm.localescape calls in the entry block.  Even
    // if we aren't splitting the block, it's nice for allocas to be before
    // calls.
    IP = PrepareToSplitEntryBlock(BB, IP);
  } else {
    EntryLoc = IP->getDebugLoc();
    if (!EntryLoc)
      if (auto *SP = F.getSubprogram())
        EntryLoc = DILocation::get(SP->getContext(), 0, 0, SP);
  }

  IRBuilder<> IRB(&*IP);
  IRB.SetCurrentDebugLocation(EntryLoc);
  if (Options.TracePC) {
    IRB.CreateCall(SanCovTracePC)
        ->setCannotMerge(); // gets the PC using GET_CALLER_PC.
  }
  if (Options.TracePCGuard) {
    auto GuardPtr = IRB.CreateIntToPtr(
        IRB.CreateAdd(IRB.CreatePointerCast(FunctionGuardArray, IntptrTy),
                      ConstantInt::get(IntptrTy, Idx * 4)),
        Int32PtrTy);
    IRB.CreateCall(SanCovTracePCGuard, GuardPtr)->setCannotMerge();
  }
  if (Options.Inline8bitCounters) {
    auto CounterPtr = IRB.CreateGEP(
        Function8bitCounterArray->getValueType(), Function8bitCounterArray,
        {ConstantInt::get(IntptrTy, 0), ConstantInt::get(IntptrTy, Idx)});
    auto Load = IRB.CreateLoad(Int8Ty, CounterPtr);
    auto Inc = IRB.CreateAdd(Load, ConstantInt::get(Int8Ty, 1));
    auto Store = IRB.CreateStore(Inc, CounterPtr);
    SetNoSanitizeMetadata(Load);
    SetNoSanitizeMetadata(Store);
  }
  if (Options.InlineBoolFlag) {
    auto FlagPtr = IRB.CreateGEP(
        FunctionBoolArray->getValueType(), FunctionBoolArray,
        {ConstantInt::get(IntptrTy, 0), ConstantInt::get(IntptrTy, Idx)});
    auto Load = IRB.CreateLoad(Int1Ty, FlagPtr);
    auto ThenTerm =
        SplitBlockAndInsertIfThen(IRB.CreateIsNull(Load), &*IP, false);
    IRBuilder<> ThenIRB(ThenTerm);
    auto Store = ThenIRB.CreateStore(ConstantInt::getTrue(Int1Ty), FlagPtr);
    SetNoSanitizeMetadata(Load);
    SetNoSanitizeMetadata(Store);
  }
  if (Options.StackDepth && IsEntryBB && !IsLeafFunc) {
    // Check stack depth.  If it's the deepest so far, record it.
    Module *M = F.getParent();
    Function *GetFrameAddr = Intrinsic::getDeclaration(
        M, Intrinsic::frameaddress,
        IRB.getInt8PtrTy(M->getDataLayout().getAllocaAddrSpace()));
    auto FrameAddrPtr =
        IRB.CreateCall(GetFrameAddr, {Constant::getNullValue(Int32Ty)});
    auto FrameAddrInt = IRB.CreatePtrToInt(FrameAddrPtr, IntptrTy);
    auto LowestStack = IRB.CreateLoad(IntptrTy, SanCovLowestStack);
    auto IsStackLower = IRB.CreateICmpULT(FrameAddrInt, LowestStack);
    auto ThenTerm = SplitBlockAndInsertIfThen(IsStackLower, &*IP, false);
    IRBuilder<> ThenIRB(ThenTerm);
    auto Store = ThenIRB.CreateStore(FrameAddrInt, SanCovLowestStack);
    SetNoSanitizeMetadata(LowestStack);
    SetNoSanitizeMetadata(Store);
  }
}

std::string
ModuleSanitizerCoverage::getSectionName(const std::string &Section) const {
  if (TargetTriple.isOSBinFormatCOFF()) {
    if (Section == SanCovCountersSectionName)
      return ".SCOV$CM";
    if (Section == SanCovBoolFlagSectionName)
      return ".SCOV$BM";
    if (Section == SanCovPCsSectionName)
      return ".SCOVP$M";
    return ".SCOV$GM"; // For SanCovGuardsSectionName.
  }
  if (TargetTriple.isOSBinFormatMachO())
    return "__DATA,__" + Section;
  return "__" + Section;
}

std::string
ModuleSanitizerCoverage::getSectionStart(const std::string &Section) const {
  if (TargetTriple.isOSBinFormatMachO())
    return "\1section$start$__DATA$__" + Section;
  return "__start___" + Section;
}

std::string
ModuleSanitizerCoverage::getSectionEnd(const std::string &Section) const {
  if (TargetTriple.isOSBinFormatMachO())
    return "\1section$end$__DATA$__" + Section;
  return "__stop___" + Section;
}

char ModuleSanitizerCoverageLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(ModuleSanitizerCoverageLegacyPass, "sancov",
                      "Pass for instrumenting coverage on functions", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(PostDominatorTreeWrapperPass)
INITIALIZE_PASS_END(ModuleSanitizerCoverageLegacyPass, "sancov",
                    "Pass for instrumenting coverage on functions", false,
                    false)
ModulePass *llvm::createModuleSanitizerCoverageLegacyPassPass(
    const SanitizerCoverageOptions &Options,
    const std::vector<std::string> &AllowlistFiles,
    const std::vector<std::string> &BlocklistFiles) {
  return new ModuleSanitizerCoverageLegacyPass(Options, AllowlistFiles,
                                               BlocklistFiles);
}


/*###################################*/
/*##  aflgo-Fuction-Implementation ##*/
/*###################################*/
static void getDebugLoc(const Instruction *I, std::string &Filename,
                        unsigned &Line) {
#ifdef LLVM_OLD_DEBUG_API
  DebugLoc Loc = I->getDebugLoc();
  if (!Loc.isUnknown()) {
    llvm::errs() <<"getDebugLoc_1 Loc.isknown" << "\n\n";
    DILocation cDILoc(Loc.getAsMDNode(M.getContext()));
    DILocation oDILoc = cDILoc.getOrigLocation();

    Line = oDILoc.getLineNumber();
    Filename = oDILoc.getFilename().str();

    if (filename.empty()) {
      Line = cDILoc.getLineNumber();
      Filename = cDILoc.getFilename().str();
      llvm::errs() << "getDebugLoc_1 filename is"<< Filename << "\n\n";
      llvm::errs() << "getDebugLoc_1 Line is"<< Line << "\n\n";
    }
  }
#else
  if (DILocation *Loc = I->getDebugLoc()) {
    Line = Loc->getLine();
    Filename = Loc->getFilename().str();
    if(Filename == "main.rs") {
      llvm::errs() << "getDebugLoc_2 filename is " << Filename <<"\n\n";
      llvm::errs() << "getDebugLoc_2 Line is " << Line <<"\n\n";
    }



    if (Filename.empty()) {
      DILocation *oDILoc = Loc->getInlinedAt();
      if (oDILoc) {
        Line = oDILoc->getLine();
        Filename = oDILoc->getFilename().str();
      }
    }
  }
#endif /* LLVM_OLD_DEBUG_API */
}

static bool isBlacklisted(const Function *F) {
  static const SmallVector<std::string, 8> Blacklist = {
    "asan.",
    "llvm.",
    "sancov.",
    "__ubsan_handle_",
    "free",
    "malloc",
    "calloc",
    "realloc"
  };

  for (auto const &BlacklistFunc : Blacklist) {
    if (F->getName().startswith(BlacklistFunc)) {
      return true;
    }
  }

  return false;
}