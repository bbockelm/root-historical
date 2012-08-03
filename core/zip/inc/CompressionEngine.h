#ifndef ROOT_CompressionEngine
#define ROOT_CompressionEngine
/*
 * A simple set of classes for managing compression engines and state.
 * Brian Bockelman, 2012
 */

#include <memory>

// CINT honks on zlib.
#if !defined(__CINT__)
#include <zlib.h>
#endif

#include "TBuffer.h"

namespace ROOT {

class CompressionEngine;

class CompressionEngineFactory {

public:

  enum ECompressionType { kDefault=0, kZIP=1, kLZMA=2 };

  CompressionEngineFactory() {}

  // Create a compression engine of a given type.
  std::auto_ptr<CompressionEngine> Create(ECompressionType kind=kDefault);

};

class CompressionEngine {

public:

  virtual ~CompressionEngine() {}

  // Set the compression level of the engine.  The value of "level" is implementation specific.
  // Returns false if the requested level is invalid.
  virtual bool SetCompressionLevel(int) = 0;

  // Set the amount of memory used by the compression engine.  The value of "level" is implementation specific.
  // Returns false if the requested level is invalid.
  virtual bool SetMemoryLevel(int) = 0;

  // An upper bound on the memory this compression will use; value should be in bytes.
  virtual size_t GetMemoryEstimate() = 0;

  // Return the maximum allowed value for the memory level.
  virtual size_t GetMaxMemoryLevel() = 0;

  // Return, in bytes, the current window size.
  virtual size_t GetWindowSize() = 0;

  // Compress the data in a given TBuffer to an output TBuffer.
  // Returns the number of bytes compressed, or -1 on error.
  // In such a case, the contents of the output TBuffer are undefined.
  // The resulting output buffer should be able to be decompressed stand-alone, and has the ROOT-specific
  // header.
  virtual ssize_t Compress(const TBuffer & in, off_t input_offset, TBuffer& out, off_t output_offset) = 0;

  // Reset the state of the compression engine.  Invoke Reset between calls to reset if you will be
  // compressing drastically different data.  By not calling Reset for similar data (i.e., the same
  // baskets on a branch), you will have improved compression ratios.
  virtual void Reset() = 0;

};

class ZipCompressionEngine : public CompressionEngine {

public:

  ZipCompressionEngine();

  // Any level between 0 and 9 is acceptable; the higher, the better the compression
  // (and the higher the CPU costs).  0 makes this library a no-op.
  virtual bool SetCompressionLevel(int level) { if (level < 0 || level > 9) return false; m_level = level; return true;}

  // The amount of memory which will be held by the compression object:
  // 3: 256KB memory will be used
  // 2: 128KB
  // 1: 64KB
  // 0: 32KB
  virtual bool SetMemoryLevel(int memory) { if (memory < 0 || memory > 3) return false; m_memory = memory; return true;}
  virtual size_t GetMaxMemoryLevel() {return 3;}
  virtual size_t GetWindowSize() {return 1 << (15+m_memory-3);}
  virtual ssize_t Compress(const TBuffer &in, off_t input_offset, TBuffer &out, off_t output_offset);
  virtual void Reset();
  virtual size_t GetMemoryEstimate();

  virtual ~ZipCompressionEngine();

private:

#if defined(__CINT__)
  void * m_stream;
#else
  z_stream m_stream;
#endif
  unsigned char m_level;
  unsigned char m_memory;
  bool m_initialized;

};

}

#endif
