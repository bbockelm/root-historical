// @(#)root/utils:$Id$
// Author: Philippe Canal 27/08/2003

/*************************************************************************
 * Copyright (C) 1995-2003, Rene Brun, Fons Rademakers, and al.          *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS              *
 *************************************************************************/

#include "RConfigure.h"
#include "RConfig.h"
#include "Api.h"

#include "RClStl.h"
#include "TClassEdit.h"
using namespace TClassEdit;
#include <stdio.h>

#include "clang/AST/Decl.h"
#include "clang/AST/DeclTemplate.h"

#include "Scanner.h"

// From the not-existing yet rootcint.h
void WriteClassInit(const RScanner::AnnotatedRecordDecl &decl);
void WriteAuxFunctions(const RScanner::AnnotatedRecordDecl &decl);
std::string R__GetQualifiedName(const clang::NamedDecl *cl);

int ElementStreamer(G__TypeInfo &ti,const char *R__t,int rwmode,const char *tcl=0);

#ifndef ROOT_Varargs
#include "Varargs.h"
#endif
void Error(const char *location, const char *va_(fmt), ...);
void Warning(const char *location, const char *va_(fmt), ...);

//
// ROOT::RStl is the rootcint STL handling class.
//

static int fgCount = 0;

ROOT::RStl& ROOT::RStl::inst()
{
   // Return the singleton ROOT::RStl.

   static ROOT::RStl instance;
   return instance;

}

void ROOT::RStl::GenerateTClassFor(const char *requestedName, const clang::CXXRecordDecl *stlclass)
{
   // Force the generation of the TClass for the given class.

   const clang::ClassTemplateSpecializationDecl *templateCl = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(stlclass);

   if (templateCl == 0) {
      Error("RStl::GenerateTClassFor","%s not in a template",
            R__GetQualifiedName(stlclass).c_str());      
   }
   
   
   if ( TClassEdit::STLKind( stlclass->getName().data() )  == TClassEdit::kVector ) {
      const clang::TemplateArgument &arg( templateCl->getTemplateArgs().get(0) );
      if (arg.getKind() == clang::TemplateArgument::Type) {
         const clang::NamedDecl *decl = arg.getAsType().getTypePtr()->getAsCXXRecordDecl();
         if (decl) {
            llvm::StringRef argname = decl->getName();
            if ( 0 == strcmp(argname.data(),"bool") || 0 == strcmp(argname.data(),"Bool_t") ) {
               Warning("std::vector<bool>", " is not fully supported yet!\nUse std::vector<char> or std::deque<bool> instead.\n");
            }
         }
      }
   }
   
   fList.insert( RScanner::AnnotatedRecordDecl(++fgCount,stlclass,requestedName,true,false,false,false) );
   
   for(unsigned int i=0; i <  templateCl->getTemplateArgs().size(); ++i) {
      const clang::TemplateArgument &arg( templateCl->getTemplateArgs().get(i) );
      if (arg.getKind() == clang::TemplateArgument::Type) {
         const clang::NamedDecl *decl = arg.getAsType().getTypePtr()->getAsCXXRecordDecl();
      
         if (decl && TClassEdit::STLKind( decl->getName().data() ) != 0 )
         {
            const clang::CXXRecordDecl *clxx = llvm::dyn_cast<clang::CXXRecordDecl>(decl);
            if (clxx) {
               GenerateTClassFor( "", clxx );
            }
         }
      }
   }

   //    fprintf(stderr,"ROOT::RStl registered %s as %s\n",stlclassname.c_str(),registername.c_str());
}

void ROOT::RStl::Print()
{
   // Print the content of the object
   fprintf(stderr,"ROOT::RStl singleton\n");
   list_t::iterator iter;
   for(iter = fList.begin(); iter != fList.end(); ++iter) {
      fprintf(stderr, "need TClass for %s\n", R__GetQualifiedName((*iter)).c_str());
   }
}

std::string ROOT::RStl::DropDefaultArg(const std::string &classname)
{
   // Remove the default argument from the stl container.

   G__ClassInfo cl(classname.c_str());

   if ( cl.TmpltName() == 0 ) return classname;

   if ( TClassEdit::STLKind( cl.TmpltName() ) == 0 ) return classname;

   return TClassEdit::ShortType( cl.Fullname(),
                                 TClassEdit::kDropStlDefault );

}

void ROOT::RStl::WriteClassInit(FILE* /*file*/)
{
   // This function writes the TGeneraticClassInfo initialiser
   // and the auxiliary functions (new and delete wrappers) for
   // each of the STL containers that have been registered

   list_t::iterator iter;
   for(iter = fList.begin(); iter != fList.end(); ++iter) {
      ::WriteClassInit( *iter );
      ::WriteAuxFunctions( *iter );
   }
}

void ROOT::RStl::WriteStreamer(FILE *file, G__ClassInfo &stlcl)
{
   // Write the free standing streamer function for the given
   // STL container class.

   std::string streamerName = "stl_streamer_";

   std::string shortTypeName = GetLong64_Name( TClassEdit::ShortType(stlcl.Name(),TClassEdit::kDropStlDefault) );
   std::string noConstTypeName( TClassEdit::CleanType(shortTypeName.c_str(),2) );

   streamerName += G__map_cpp_name((char *)shortTypeName.c_str());
   std::string typedefName = G__map_cpp_name((char *)shortTypeName.c_str());

   int nestedLoc=0;
   std::vector<std::string> splitName;
   TClassEdit::GetSplit(shortTypeName.c_str(),splitName,nestedLoc);

   int stltype = TClassEdit::STLKind(splitName[0].c_str());

   G__TypeInfo firstType(splitName[1].c_str());
   G__TypeInfo secondType;
   const char *tclFirst=0,*tclSecond=0;
   std::string firstFullName, secondFullName;

   if (ElementStreamer(firstType,0,0)) {
      tclFirst = "R__tcl1";
      const char *name = firstType.Fullname();
      if (name) {
         // the value return by ti.Fullname is a static buffer
         // so we have to copy it immeditately
         firstFullName = TClassEdit::ShortType(name,TClassEdit::kDropStlDefault);
      } else {
         // ti is a simple type name
         firstFullName = firstType.TrueName();
      }
   }
   if (stltype==kMap || stltype==kMultiMap) {
      secondType.Init( splitName[2].c_str());

      if (ElementStreamer(secondType,0,0)) {
         tclSecond="R__tcl2";
         const char *name = secondType.Fullname();
         if (name) {
            // the value return by ti.Fullname is a static buffer
            // so we have to copy it immeditately
            secondFullName = TClassEdit::ShortType(name,TClassEdit::kDropStlDefault);
         } else {
            // ti is a simple type name
            secondFullName = secondType.TrueName();
         }
      }
   }

   fprintf(file, "//___________________________________________________________");
   fprintf(file, "_____________________________________________________________\n");
   fprintf(file, "namespace ROOT {\n");
   fprintf(file, "   typedef %s %s;\n",shortTypeName.c_str(), typedefName.c_str());
   fprintf(file, "   static void %s(TBuffer &R__b, void *R__p)\n",streamerName.c_str());
   fprintf(file, "   {\n");
   fprintf(file, "      if (gDebug>1) Info(__FILE__,\"Running compiled streamer for %s at %%p\",R__p);\n",shortTypeName.c_str());
   fprintf(file, "      %s &R__stl = *(%s *)R__p;\n",shortTypeName.c_str(),shortTypeName.c_str());
   fprintf(file, "      if (R__b.IsReading()) {\n");
   fprintf(file, "         R__stl.clear();\n");

   if (tclFirst)
      fprintf(file, "         TClass *R__tcl1 = TBuffer::GetClass(typeid(%s));\n",
              firstFullName.c_str());
   if (tclSecond)
      fprintf(file, "         TClass *R__tcl2 = TBuffer::GetClass(typeid(%s));\n",
              secondFullName.c_str());

   fprintf(file, "         int R__i, R__n;\n");
   fprintf(file, "         R__b >> R__n;\n");

   if (stltype==kVector) {
      fprintf(file,"         R__stl.reserve(R__n);\n");
   }
   fprintf(file, "         for (R__i = 0; R__i < R__n; R__i++) {\n");

   ElementStreamer(firstType,"R__t",0,tclFirst);
   if (stltype == kMap || stltype == kMultiMap) {     //Second Arg
      ElementStreamer(secondType,"R__t2",0,tclSecond);
   }
   switch (stltype) {

      case kMap:
      case kMultiMap:
         fprintf(file, "            R__stl.insert(make_pair(R__t,R__t2));\n");
         break;
      case kSet:
      case kMultiSet:
         fprintf(file, "            R__stl.insert(R__t);\n");
         break;
      case kVector:
      case kList:
      case kDeque:
         fprintf(file, "            R__stl.push_back(R__t);\n");
         break;

      default:
            assert(0);
   }
   fprintf(file, "         }\n");

   fprintf(file, "      } else {\n");

   fprintf(file, "         int R__n=(&R__stl) ? int(R__stl.size()) : 0;\n");
   fprintf(file, "         R__b << R__n;\n");
   fprintf(file, "         if(R__n) {\n");

   if (tclFirst) {
      fprintf(file, "            TClass *R__tcl1 = TBuffer::GetClass(typeid(%s));\n",
              firstFullName.c_str());
      fprintf(file, "            if (R__tcl1==0) {\n");
      fprintf(file, "               Error(\"%s streamer\",\"Missing the TClass object for %s!\");\n",
              shortTypeName.c_str(), firstFullName.c_str());
      fprintf(file, "               return;\n");
      fprintf(file, "            }\n");
   }
   if (tclSecond) {
      fprintf(file, "            TClass *R__tcl2 = TBuffer::GetClass(typeid(%s));\n",
              secondFullName.c_str());
      fprintf(file, "            if (R__tcl2==0) {\n");
      fprintf(file, "               Error(\"%s streamer\",\"Missing the TClass object for %s!\");\n",
              shortTypeName.c_str(), secondFullName.c_str());
      fprintf(file, "               return;\n");
      fprintf(file, "            }\n");
   }
   fprintf(file, "            %s::iterator R__k;\n", shortTypeName.c_str());
   fprintf(file, "            for (R__k = R__stl.begin(); R__k != R__stl.end(); ++R__k) {\n");

   if (stltype == kMap || stltype == kMultiMap) {
      ElementStreamer(firstType ,"((*R__k).first )",1,tclFirst);
      ElementStreamer(secondType,"((*R__k).second)",1,tclSecond);
   } else {
      ElementStreamer(firstType ,"(*R__k)"         ,1,tclFirst);
   }

   fprintf(file, "            }\n");
   fprintf(file, "         }\n");

   fprintf(file, "      }\n");
   fprintf(file, "   } // end of %s streamer\n",stlcl.Fullname());
   fprintf(file, "} // close namespace ROOT\n\n");

   fprintf(file, "// Register the streamer (a typedef is used to avoid problem with macro parameters\n");

   //if ( 0 != ::getenv("MY_ROOT") && ::getenv("MY_ROOT")[0]>'1' )  {
   //  fprintf(file, "// Disabled due customized build:\n// ");
   //}
   fprintf(file, "RootStlStreamer(%s,%s)\n", typedefName.c_str(), streamerName.c_str());
   fprintf(file, "\n");

}

void ROOT::RStl::WriteStreamer(FILE *file)
{
   // Write the free standing streamer function for the registereed
   // STL container classes

   list_t::iterator iter;
   G__ClassInfo cl;
   for(iter = fList.begin(); iter != fList.end(); ++iter) {
      cl.Init( R__GetQualifiedName((*iter)).c_str() );
      WriteStreamer(file,cl);
   }
}
