/*
 * @Author Andrew Bao
 * @email: bao00065@umn.edu
 *
 */

#include "CallGraph.h"

using namespace llvm;


void CallGraph::addEdge(Function *srcNode, FuncSet &destNode){
	
      for (auto callee: destNode){
          Ctx->CalleeMaps[srcNode].insert(callee);

      }	  
}


void CallGraph::addCallerMap(Function *callee, CallBase* CB){
      Ctx->CallerMaps[callee].insert(CB);

}


void CallGraph::addNode(Function *Node, IdentifierType ID){
     Ctx->FunIdMap.insert(std::make_pair(Node, ID));
}


void CallGraph::addValToFuncMap(Function *func) { Ctx->ValToFuncMap[func] = func; }


void CallGraph::printNode(){
    for (auto pairs: Ctx->FunIdMap){
        errs() << "NODE is:"<< pairs.first->getName().str() << ": " <<pairs.second << "\n";
    }   
}

std::string getFunFileName(Function &F) {
    // Iterate over all basic blocks in the function
    for (const auto &BB : F) {
        // Iterate over all instructions in the basic block
        for (const auto &I : BB) {
            if (DILocation *Loc = I.getDebugLoc()) { // Get debug location, if available
               std::string filename = Loc->getFilename().str();
                if (!filename.empty()) {
                    return filename; // Return the filename if it's not empty
                }
            }
        }
    }

    return ""; // Return an empty string if no debug location with a filename is found
}


// Original printCallGraph();;;;
// void CallGraph::printCallGraph(){
  
//   std::string dotfiles( Ctx->DotFile + "/dot-files");
//   if (!llvm::sys::fs::exists(dotfiles)){
//     llvm::sys::fs::create_directory(dotfiles);
//   }

//   std::string GraphName( dotfiles + "/callgraph.dot");
//   std::error_code error;
//   raw_fd_ostream O(GraphName, error);

//   O << "digraph \"Call Graph\"{\n";
//   O << "label=\"Call Graph\";\n";

//   for (auto pairs : Ctx->FunIdMap){

//     Function *F = pairs.first;
//     unsigned id = pairs.second;
//     //errs() << F->getName().str() << ":" << id<< "\n";    
//      O << id << " [shape=record, filename=\"{" << getFunFileName(*F) << "}\"" << "label=\"{" << F->getName().str()
//       << "}\", indirect callsite=\"{";

//     if (!Ctx->IndirectCallSite[F].empty()){
//        for (auto callInst: Ctx->IndirectCallSite[F]){
//            DILocation *Loc = callInst->getDebugLoc();
//            string filename = Loc->getFilename().str();
//            unsigned line = Loc->getLine();
//            O << " " << filename << ":" << line << " "; 
//        }
//     } 
//     O << "}\"];\n";
    
//     //Output direct call
//     if (!Ctx->CalleeMaps[F].empty()){
//        for (auto callee: Ctx->CalleeMaps[F]){
// 	   if (Ctx->FunIdMap.find(callee) != Ctx->FunIdMap.end()) {   
//               IdentifierType calleeID = Ctx->FunIdMap[callee];
//               O << id << " -> " << calleeID <<";\n";
// 	   }   
//        }   
//     }
//      /*Indirect call case 1:
//      *
//      * When alias analysis is empty but type analysis is not empty, we directly 
//      * use the result from type analysis. 
//      *
//      */

    
//     if (Ctx->AliasIndirectCallMap[F].empty() && !Ctx->TypeIndirectCallMap[F].empty()){
//        for (auto TypeCallee: Ctx->TypeIndirectCallMap[F]){ 
//             if (Ctx->FunIdMap.find(TypeCallee) != Ctx->FunIdMap.end()){
//                IdentifierType calleeID = Ctx->FunIdMap[TypeCallee];
//                O << id << " -> " << calleeID <<";\n";
// 	    }
//       }
//     }
//     /*Indirect call case 2:
//      *
//      *When alias analysis and type analysis are not empty, usually, the result from alias
//      *analysis is the subset of type analysis.
//      *
//      */
//     else if (!Ctx->AliasIndirectCallMap[F].empty() && ! Ctx->TypeIndirectCallMap[F].empty()){
//        for (auto TypeCallee: Ctx->TypeIndirectCallMap[F]){ 
//           for (auto AliasCallee: Ctx->AliasIndirectCallMap[F]){
//                if (AliasCallee == TypeCallee){
// 		  if (Ctx->FunIdMap.find(AliasCallee) != Ctx->FunIdMap.end()){    
//                      IdentifierType calleeID = Ctx->FunIdMap[AliasCallee];
//                      O << id << " -> " << calleeID <<";\n";
// 		  }    
//                }
//           }
//        }   
//     } 

//   }
//   O << "}\n";
//   O.close();
// }



static inline std::string esc(StringRef s) {
  std::string r; r.reserve(s.size());
  for (char c : s) { if (c=='"' || c=='\\') r.push_back('\\'); r.push_back(c); }
  return r;
}

// DILocation "unknown")
static inline StringRef safeFilename(const DILocation *Loc) {
  if (!Loc) return "unknown";
  if (auto *Scope = Loc->getScope()) {
    if (auto *DS = dyn_cast<DIScope>(Scope)) {
      if (auto *File = DS->getFile()) {
        StringRef F = File->getFilename();
        if (!F.empty()) return F;
      }
    }
  }
  StringRef F2 = Loc->getFilename();
  return F2.empty() ? StringRef("unknown") : F2;
}

void CallGraph::printCallGraph() {

  std::string dotdir = Ctx->DotFile + std::string("/dot-files");
  // 
  sys::fs::create_directories(dotdir);

  std::string GraphName = dotdir + "/callgraph.dot";
  std::error_code ec;
  raw_fd_ostream O(GraphName, ec);
  if (ec) {
    errs() << "[printCallGraph] open failed: " << GraphName
           << " : " << ec.message() << "\n";
    return;
  }

  O << "digraph \"Call Graph\"{\n";
  O << "label=\"Call Graph\";\n";

  for (auto &pairs : Ctx->FunIdMap) {
    Function *F = pairs.first;
    unsigned id = pairs.second;

    // 
    std::string fnFile = getFunFileName(*F);
    O << id
      << " [shape=record, filename=\"{" << esc(fnFile) << "}\""
      << " label=\"{" << esc(F->getName()) << "}\", indirect callsite=\"{";

    // 
    auto it = Ctx->IndirectCallSite.find(F);
    if (it != Ctx->IndirectCallSite.end() && !it->second.empty()) {
      for (auto *CI : it->second) {
        if (!CI) continue;

        DebugLoc DL = CI->getDebugLoc();   // 
        if (!DL) continue;                 // 

        const DILocation *Loc = DL.get();
        if (!Loc) continue;

        unsigned line = Loc->getLine();
        if (line == 0) continue;           // 

        StringRef file = safeFilename(Loc);
        O << " " << esc(file) << ":" << line << " ";
      }
    }
    O << "}\"];\n";

    // Direct callees
    auto itCallees = Ctx->CalleeMaps.find(F);
    if (itCallees != Ctx->CalleeMaps.end() && !itCallees->second.empty()) {
      for (auto *Callee : itCallees->second) {
        auto itID = Ctx->FunIdMap.find(Callee);
        if (itID != Ctx->FunIdMap.end())
          O << id << " -> " << itID->second << ";\n";
      }
    }

    // Indirect (case 1): 
    auto &AliasMap = Ctx->AliasIndirectCallMap[F]; // operator[]
    auto &TypeMap  = Ctx->TypeIndirectCallMap[F];
    if (AliasMap.empty() && !TypeMap.empty()) {
      for (auto *TC : TypeMap) {
        auto itID = Ctx->FunIdMap.find(TC);
        if (itID != Ctx->FunIdMap.end())
          O << id << " -> " << itID->second << ";\n";
      }
    }
    // Indirect (case 2): 
    else if (!AliasMap.empty() && !TypeMap.empty()) {
      // 
      for (auto *TC : TypeMap) {
        for (auto *AC : AliasMap) {
          if (AC == TC) {
            auto itID = Ctx->FunIdMap.find(AC);
            if (itID != Ctx->FunIdMap.end())
              O << id << " -> " << itID->second << ";\n";
          }
        }
      }
    }
  }

  O << "}\n";
  // 
}