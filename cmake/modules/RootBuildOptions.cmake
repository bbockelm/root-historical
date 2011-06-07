set(root_build_options)

#---------------------------------------------------------------------------------------------------
#---ROOT_BUILD_OPTION( name defvalue [description] )
#---------------------------------------------------------------------------------------------------
function(ROOT_BUILD_OPTION name defvalue)
  if(ARGN)
    set(description ${ARGN})
  else()
    set(description " ")
  endif()    
  option(${name} "${description}" ${defvalue})
  set(root_build_options ${root_build_options} ${name} PARENT_SCOPE )
endfunction()

#---------------------------------------------------------------------------------------------------
#---ROOT_SHOW_OPTIONS([var] )
#---------------------------------------------------------------------------------------------------
function(ROOT_SHOW_OPTIONS)
  set(enabled)
  foreach(opt ${root_build_options})
    if(${opt})
      set(enabled "${enabled} ${opt}")
    endif()
  endforeach()
  if(NOT ARGN)
    message(STATUS "Enabled support for: ${enabled}")
  else()
    set(${ARGN} "${enabled}" PARENT_SCOPE)
  endif()
endfunction()

#---------------------------------------------------------------------------------------------------
#---ROOT_WRITE_OPTIONS(file )
#---------------------------------------------------------------------------------------------------
function(ROOT_WRITE_OPTIONS file)
  file(WRITE ${file} "#---Options enabled for the build of ROOT-----------------------------------------------\n")
  foreach(opt ${root_build_options})
    if(${opt})
      file(APPEND ${file} "set(${opt} ON)\n")
    else()
      file(APPEND ${file} "set(${opt} OFF)\n")
    endif()
  endforeach()
endfunction()

#---Define default values depending on platform before the options are defined----------------------
if(WIN32)
  set(x11_defvalue OFF)
  set(memstat_defvalue OFF)
  set(explicitlink_defvalue ON)
else()
  set(x11_defvalue ON)
  set(memstat_defvalue ON)
  set(explicitlink_defvalue OFF)
endif()

ROOT_BUILD_OPTION(afs OFF "AFS support, requires AFS libs and objects")                 
ROOT_BUILD_OPTION(alien OFF "AliEn support, requires libgapiUI from ALICE")               
ROOT_BUILD_OPTION(asimage ON "Image processing support, requires libAfterImage")             
ROOT_BUILD_OPTION(astiff ON "Include tiff support in image processing")
ROOT_BUILD_OPTION(bonjour ON "Bonjour support, requires libdns_sd and/or Avahi")            
ROOT_BUILD_OPTION(builtin_afterimage ON "Built included libAfterImage, or use system libAfterImage")
ROOT_BUILD_OPTION(builtin_ftgl ON "Built included libFTGL, or use system libftgl")        
ROOT_BUILD_OPTION(builtin_freetype OFF "Built included libfreetype, or use system libfreetype")   
ROOT_BUILD_OPTION(builtin_glew ON "Built included libGLEW, or use system libGLEW")
ROOT_BUILD_OPTION(builtin_pcre ON "Built included libpcre, or use system libpcre")        
ROOT_BUILD_OPTION(builtin_zlib OFF "Built included libz, or use system libz")        
ROOT_BUILD_OPTION(castor ON "CASTOR support, requires libshift from CASTOR >= 1.5.2")             
ROOT_BUILD_OPTION(chirp ON "Chirp support (Condor remote I/O), requires libchirp_client")               
ROOT_BUILD_OPTION(cintex ON "Build the libCintex Reflex interface library")              
ROOT_BUILD_OPTION(clarens OFF "Clarens RPC support, optionally used by PROOF")
ROOT_BUILD_OPTION(cling OFF "Enable new CLING C++ interpreter")            
ROOT_BUILD_OPTION(dcache OFF "dCache support, requires libdcap from DESY")              
ROOT_BUILD_OPTION(exceptions ON "Turn on compiler exception handling capability")         
ROOT_BUILD_OPTION(explicitlink ${explicitlink_defvalue} "Explicitly link with all dependent libraries")      
ROOT_BUILD_OPTION(fftw3 OFF "Fast Fourier Transform support, requires libfftw3") 
ROOT_BUILD_OPTION(fitsio OFF "Read images and data from FITS files, requires cfitsio")                
ROOT_BUILD_OPTION(gviz OFF "Graphs visualization support, requires graphviz")                
ROOT_BUILD_OPTION(gdml OFF "GDML writer and reader")                
ROOT_BUILD_OPTION(genvector ON "Build the new libGenVector library" )            
ROOT_BUILD_OPTION(gfal OFF "GFAL support, requires libgfal")        
ROOT_BUILD_OPTION(glite OFF "gLite support, requires libglite-api-wrapper v.3 from GSI (https://subversion.gsi.de/trac/dgrid/wiki)")              
ROOT_BUILD_OPTION(globus OFF "Globus authentication support, requires Globus toolkit")              
ROOT_BUILD_OPTION(gsl_shared OFF "Enable linking against shared libraries for GSL (default no)")
ROOT_BUILD_OPTION(hdfs OFF "HDFS support; requires libhdfs from HDFS >= 0.19.1")         
ROOT_BUILD_OPTION(krb5 ON "Kerberos5 support, requires Kerberos libs")               
ROOT_BUILD_OPTION(ldap ON "LDAP support, requires (Open)LDAP libs")                
ROOT_BUILD_OPTION(mathmore OFF "Build the new libMathMore extended math library, requires GSL (vers. >= 1.8)")            
ROOT_BUILD_OPTION(memstat ${memstat_defvalue} "A memory statistics utility, helps to detect memory leaks")    
ROOT_BUILD_OPTION(minuit2 OFF "Build the new libMinuit2 minimizer library")            
ROOT_BUILD_OPTION(monalisa OFF "Monalisa monitoring support, requires libapmoncpp")        
ROOT_BUILD_OPTION(mysql OFF "MySQL support, requires libmysqlclient")              
ROOT_BUILD_OPTION(odbc ON "ODBC support, requires libiodbc or libodbc")                
ROOT_BUILD_OPTION(opengl ON "OpenGL support, requires libGL and libGLU")              
ROOT_BUILD_OPTION(oracle OFF "Oracle support, requires libocci")              
ROOT_BUILD_OPTION(pch  OFF)   
ROOT_BUILD_OPTION(peac OFF "PEAC, PROOF Enabled Analysis Center, requires Clarens")       
ROOT_BUILD_OPTION(pgsql OFF "PostgreSQL support, requires libpq")               
ROOT_BUILD_OPTION(pythia6 OFF "Pythia6 EG support, requires libPythia6")            
ROOT_BUILD_OPTION(pythia8 OFF "Pythia8 EG support, requires libPythia8")             
ROOT_BUILD_OPTION(python ON "Python ROOT bindings, requires python >= 2.2")              
ROOT_BUILD_OPTION(qt OFF "Qt graphics backend, requires libqt >= 4.x")                
ROOT_BUILD_OPTION(qtgsi OFF "GSI's Qt integration, requires libqt >= 3")               
ROOT_BUILD_OPTION(reflex ON "Build the libReflex dictionary library")              
ROOT_BUILD_OPTION(roofit OFF "Build the libRooFit advanced fitting package")              
ROOT_BUILD_OPTION(ruby OFF "Ruby ROOT bindings, requires ruby >= 1.8")                
ROOT_BUILD_OPTION(rfio OFF "RFIO support, requires libshift from CASTOR >= 1.5.2")                
ROOT_BUILD_OPTION(rpath ON "Set run-time library load path on executables")              
ROOT_BUILD_OPTION(sapdb OFF "MaxDB/SapDB support, requires libsqlod and libsqlrte")              
ROOT_BUILD_OPTION(shadowpw OFF "Shadow password support")           
ROOT_BUILD_OPTION(shared ON "Use shared 3rd party libraries if possible")              
ROOT_BUILD_OPTION(soversion OFF "Set version number in sonames (recommended)")           
ROOT_BUILD_OPTION(srp OFF "SRP support, requires SRP source tree")       
ROOT_BUILD_OPTION(ssl ON "SSL encryption support, requires openssl")                
ROOT_BUILD_OPTION(table OFF "Build libTable contrib library")             
ROOT_BUILD_OPTION(tmva ON "Build TMVA multi variate analysis library")             
ROOT_BUILD_OPTION(unuran OFF "UNURAN - package for generating non-uniform random numbers")              
ROOT_BUILD_OPTION(winrtdebug OFF "Link against the Windows debug runtime library")         
ROOT_BUILD_OPTION(xft OFF "Xft support (X11 antialiased fonts)")                
ROOT_BUILD_OPTION(xml ON "XML parser interface")
ROOT_BUILD_OPTION(x11 ${x11_defvalue} "X11 support")
ROOT_BUILD_OPTION(xrootd OFF "Build xrootd file server and its client (if supported)")
  
option(fail-on-missing "Fail the configure step if a required external package is missing" OFF)
option(minimal "Do not automatically search for support libraries" OFF)
option(gminimal "Do not automatically search for support libraries, but include X11" OFF)
  

#---General Build options----------------------------------------------------------------------
# use, i.e. don't skip the full RPATH for the build tree
set(CMAKE_SKIP_BUILD_RPATH  FALSE)

# when building, don't use the install RPATH already (but later on when installing)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE) 

# the RPATH to be used when installing
#set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")

# add the automatically determined parts of the RPATH
# which point to directories outside the build tree to the install RPATH
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

#---Avoid creating dependencies to 'non-statndard' header files -------------------------------
include_regular_expression("^[^.]+$|[.]h$|[.]icc$|[.]hxx$|[.]hpp$")

#---Set all directories where to install parts of root up to now everything is installed ------
#---according to the setting of CMAKE_INSTALL_DIR

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  message(STATUS "Setting default installation prefix CMAKE_INSTALL_PREFIX to ${ROOTSYS}")
  set(CMAKE_INSTALL_PREFIX ${ROOTSYS} CACHE PATH "Default installation of ROOT" FORCE)
endif()

#if(ROOT_INSTALL_DIR)
#  set(CMAKE_INSTALL_PREFIX ${ROOT_INSTALL_DIR})
#  add_definitions(-DR__HAVE_CONFIG)
#else()
#  set(CMAKE_INSTALL_PREFIX ${ROOTSYS})
#endif()

set(ROOT_INSTALL_DIR ${CMAKE_INSTALL_PREFIX})
set(BIN_INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/bin)
set(LIB_INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/lib)
set(INCLUDE_INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/include)
set(ETC_INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/etc)
set(DATA_INSTALL_DIR ${CMAKE_INSTALL_PREFIX})
set(DOC_INSTALL_DIR ${CMAKE_INSTALL_PREFIX})
set(MACRO_INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/macro)
set(SRC_INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/src)
set(ICON_INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/icons)
set(FONT_INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/fonts)
set(CINT_INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/cint)




