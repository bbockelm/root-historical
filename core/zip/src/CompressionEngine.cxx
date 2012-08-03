
#include "CompressionEngine.h"

#if (__GNUC__ >= 3) || defined(__INTEL_COMPILER)
#if !defined(R__unlikely)
  #define R__unlikely(expr) __builtin_expect(!!(expr), 0)
#endif
#if !defined(R__likely)
  #define R__likely(expr) __builtin_expect(!!(expr), 1)
#endif
#else
  #define R__unlikely(expr) expr
  #define R__likely(expr) expr
#endif

// Each basket contains a small header identifying the compression buffer.
#define HDRSIZE 9

using namespace ROOT;

std::auto_ptr<CompressionEngine>
CompressionEngineFactory::Create(ECompressionType compressionType)
{
  CompressionEngine * engine_p = NULL;
  std::auto_ptr<CompressionEngine> engine;
  engine.reset(NULL);
  switch (compressionType)
  {
    case kDefault:
    case kZIP:
      engine_p = new ZipCompressionEngine();
      engine.reset(engine_p);
      return engine;
    case kLZMA:
      //engine = new LzmaCompressionEngine();
    default:
      return engine;
  }
}

ZipCompressionEngine::ZipCompressionEngine()
  : m_level(9),
    m_memory(3),
    m_initialized(false)
{
}

ssize_t
ZipCompressionEngine::Compress(const TBuffer &in, off_t input_offset, TBuffer &out, off_t output_offset)
{
  if (R__unlikely(!m_initialized))
  {
    Reset();
  }

  // Make sure the output buffer can hold the compressed contents.
  size_t input_buffer_size = in.Length() - input_offset;
  out.AutoExpand(input_buffer_size);

  uLong out_so_far = m_stream.total_out;

  // Note: using C-style casts as C++ is stricter than C;
  // Bytef is basically unsigned char.
  m_stream.next_in = (Bytef*)(in.Buffer() + input_offset);
  m_stream.avail_in = input_buffer_size;
  m_stream.next_out= (Bytef*)(out.Buffer() + output_offset+ HDRSIZE);
  m_stream.avail_out = input_buffer_size;

  // For the origin of the magic numbers below, see Bits.h
  char * tgt = out.Buffer() + output_offset;
  tgt[0] = 'Z';
  tgt[1] = 'L';
  tgt[2] = (char) Z_DEFLATED;

  unsigned out_size  = m_stream.total_out - out_so_far;
  tgt[3] = (char)(out_size & 0xff);
  tgt[4] = (char)((out_size >> 8) & 0xff);
  tgt[5] = (char)((out_size >> 16) & 0xff);

  tgt[6] = (char)(input_buffer_size & 0xff);
  tgt[7] = (char)((input_buffer_size >> 8) & 0xff);
  tgt[8] = (char)((input_buffer_size >> 16) & 0xff);

  int result = deflate(&m_stream, Z_FULL_FLUSH);
  if ((result == Z_OK) && (m_stream.avail_in == 0)) {
    // Successful completion - all input written.
    return m_stream.total_out - out_so_far + HDRSIZE;
  } else {
    return -1;
  }
}

void
ZipCompressionEngine::Reset()
{
  if (m_initialized)
    // TODO: Check return value, throw exception.
    deflateEnd(&m_stream);

  // TODO: check return value.
  int memLevel = 8, windowBits = 15;
  windowBits -= (3-m_level);
  memLevel -= (3-m_level);
  deflateInit2(&m_stream, m_level, Z_DEFLATED, windowBits, memLevel, Z_DEFAULT_STRATEGY);
  m_initialized = true;
}

size_t
ZipCompressionEngine::GetMemoryEstimate()
{
  return 1 << (18+m_level-3);
}

ZipCompressionEngine::~ZipCompressionEngine()
{
  if (m_initialized)
    deflateEnd(&m_stream);
}
