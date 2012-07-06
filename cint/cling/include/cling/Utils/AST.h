//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// version: $Id$
// author:  Vassil Vassilev <vasil.georgiev.vasilev@cern.ch>
//------------------------------------------------------------------------------

#ifndef CLING_UTILS_AST_H
#define CLING_UTILS_AST_H

#include "llvm/ADT/SmallSet.h"

namespace clang {
  class ASTContext;
  class Expr;
  class DeclContext;
  class NamedDecl;
  class NamespaceDecl;
  class QualType;
  class Sema;
  class Type;
}

namespace cling {
namespace utils {
  ///\brief Class containing static utility functions synthesizing AST nodes or
  /// types.
  ///
  class Synthesize {
  public:

    ///\brief Synthesizes c-style cast in the AST from given pointer and type to
    /// cast to.
    ///
    static clang::Expr* CStyleCastPtrExpr(clang::Sema* S,
                                          clang::QualType Ty, uint64_t Ptr);
  };

  ///\brief Class containing static utility functions transforming AST nodes or
  /// types.
  ///
  class Transform {
  public:

    ///\brief "Desugars" a type while skipping the ones in the set.
    ///
    /// Desugars a given type recursively until strips all sugar or until gets a
    /// sugared type, which is to be skipped.
    ///\param[in] Ctx - The ASTContext.
    ///\param[in] QT - The type to be partially desugared.
    ///\param[in] TypesToSkip - The set of sugared types which shouldn't be 
    ///                         desugared.
    ///\returns Partially desugared QualType
    ///
    static clang::QualType GetPartiallyDesugaredType(const clang::ASTContext& Ctx, 
                                                     clang::QualType QT, 
                       const llvm::SmallSet<const clang::Type*, 4>& TypesToSkip);

  };

  class Lookup {
  public:
    static clang::NamespaceDecl* Namespace(clang::Sema* S,
                                           const char* Name,
                                           clang::DeclContext* Within = 0);
    static clang::NamedDecl* Named(clang::Sema* S,
                                   const char* Name,
                                   clang::DeclContext* Within = 0);

  };
} // end namespace utils
} // end namespace cling
#endif // CLING_UTILS_AST_H
