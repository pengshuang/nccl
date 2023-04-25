/*************************************************************************
 * Copyright (c) 2016-2019, NVIDIA CORPORATION. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#ifndef OP128_H_
#define OP128_H_

#include <type_traits>

inline __device__ void load128(const uint64_t* ptr, uint64_t &v0, uint64_t &v1) {
  asm volatile("ld.volatile.global.v2.u64 {%0,%1}, [%2];"
      : "=l"(v0), "=l"(v1) : "l"(ptr));
}

inline __device__ void store128(uint64_t* ptr, uint64_t v0, uint64_t v1) {
  asm volatile("st.volatile.global.v2.u64 [%2], {%0,%1};"
      :: "l"(v0), "l"(v1), "l"(ptr));
}

inline __device__ uint64_t* shmemCvtPtr(volatile uint64_t* shmemGenericPtr) {
  uint64_t* shmemAsmPtr;
  asm volatile("cvta.to.shared.u64 %0, %1;" : "=l"(shmemAsmPtr) : "l"(shmemGenericPtr));
  return shmemAsmPtr;
}

inline __device__ void loadShmem128(uint64_t* shmemAsmPtr, uint64_t &v0, uint64_t &v1) {
  asm volatile("ld.volatile.shared.v2.u64 {%0,%1}, [%2];"
      : "=l"(v0), "=l"(v1) : "l"(shmemAsmPtr));
}

inline __device__ void storeShmem128(uint64_t* shmemAsmPtr, uint64_t v0, uint64_t v1) {
  asm volatile("st.volatile.shared.v2.u64 [%2], {%0,%1};"
      :: "l"(v0), "l"(v1), "l"(shmemAsmPtr));
}

template<typename T>
inline __device__ void loadShmemMisaligned128(T *ptr, uint64_t &v0, uint64_t &v1) {
  union {
    uint32_t tmp4[4];
    uint64_t tmp8[2];
  };
  if(sizeof(T) < 4) {
    uint32_t *ptr4 = reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(ptr) & -uintptr_t(4));
    #pragma unroll
    for(int e=0; e < 4; e++) {
      // Produce 4 bytes of sub-register type by reading 2 4-byte
      // aligned values and shifting.
      uint32_t lo, hi;
      asm("ld.shared.b32 %0,[%1];" : "=r"(lo) : "l"(ptr4+e+0) : "memory");
      asm("ld.shared.b32 %0,[%1];" : "=r"(hi) : "l"(ptr4+e+1) : "memory");
      tmp4[e] = __funnelshift_r(lo, hi, 8*(int(reinterpret_cast<uintptr_t>(ptr))%4));
    }
  }
  else if(sizeof(T) == 4) {
    #pragma unroll
    for(int e=0; e < 4; e++)
      asm("ld.shared.b32 %0,[%1];" : "=r"(tmp4[e]) : "l"(ptr+e) : "memory");
  }
  else /*sizeof(T)==8*/ {
    #pragma unroll
    for(int e=0; e < 2; e++)
      asm("ld.shared.b64 %0,[%1];" : "=l"(tmp8[e]) : "l"(ptr+e) : "memory");
  }
  v0 = tmp8[0];
  v1 = tmp8[1];
}

template<typename T>
__device__ __forceinline__ uint32_t cvta_to_shared(T* ptr) {
  return (uint32_t)__cvta_generic_to_shared(ptr);
}
template<typename T>
__device__ __forceinline__ uintptr_t cvta_to_global(T* ptr) {
  return (uintptr_t)__cvta_generic_to_global(ptr);
}

template<typename T>
__device__ __forceinline__ T* cvta_from_shared(uint32_t shptr) {
  T* ans;
  asm("cvta.shared.u64 %0, %1;" : "=l"(ans) : "l"(uint64_t(shptr)));
  return ans;
}
template<typename T>
__device__ __forceinline__ T* cvta_from_global(uintptr_t gptr) {
  T* ans;
  asm("cvta.global.u64 %0, %1;" : "=l"(ans) : "l"(gptr));
  return ans;
}

////////////////////////////////////////////////////////////////////////////////
// BytePack<Size>: struct of bytes.

template<int Size>
union BytePack;
template<>
union BytePack<0> {};
template<>
union BytePack<1> {
  uint8_t u8[1], native;
};
template<>
union BytePack<2> {
  BytePack<1> half[2];
  uint8_t u8[2];
  uint16_t u16[1], native;
};
template<>
union BytePack<4> {
  BytePack<2> half[2];
  uint8_t u8[4];
  uint16_t u16[2];
  uint32_t u32[1], native;
};
template<>
union BytePack<8> {
  BytePack<4> half[2];
  uint8_t u8[8];
  uint16_t u16[4];
  uint32_t u32[2];
  uint64_t u64[1], native;
};
template<>
union alignas(16) BytePack<16> {
  BytePack<8> half[2];
  uint8_t u8[16];
  uint16_t u16[8];
  uint32_t u32[4];
  uint64_t u64[2];
  ulong2 ul2[1], native;
};

// Use BytePackOf<T>::Pack to get a BytePack<?> of the correct size to hold a T.
// BytePack<sizeof(T)> almost always works except when T=BytePack<0> (since
// sizeof(BytePack<0>)==1, no C++ type has zero size), that's why we have this.
template<typename T>
struct BytePackOf {
  static constexpr int Size = sizeof(T);
  using Pack = BytePack<Size>;
};
template<>
struct BytePackOf<BytePack<0>> {
  static constexpr int Size = 0;
  using Pack = BytePack<0>;
};

template<typename T>
__device__ __forceinline__ typename BytePackOf<T>::Pack toPack(T value)  {
  union { typename BytePackOf<T>::Pack p; T v; };
  v = value;
  return p;
}

template<typename T>
__device__ __forceinline__ T fromPack(typename BytePackOf<T>::Pack pack)  {
  union { typename BytePackOf<T>::Pack p; T v; };
  p = pack;
  return v;
}

////////////////////////////////////////////////////////////////////////////////
// Load/store of BytePack<?> using integral addresses.

template<int Size> __device__ BytePack<Size> ld_global(uintptr_t addr);
template<int Size> __device__ BytePack<Size> ld_volatile_global(uintptr_t addr);
template<int Size> __device__ BytePack<Size> ld_shared(uint32_t addr);
template<int Size> __device__ BytePack<Size> ld_volatile_shared(uint32_t addr);
template<int Size> __device__ void st_global(uintptr_t addr, BytePack<Size> value);
template<int Size> __device__ void st_shared(uint32_t addr, BytePack<Size> value);

template<> __device__ __forceinline__ BytePack<0> ld_global<0>(uintptr_t addr) { return {}; }
template<> __device__ __forceinline__ BytePack<0> ld_volatile_global<0>(uintptr_t addr) { return {}; }
template<> __device__ __forceinline__ BytePack<0> ld_shared<0>(uint32_t addr) { return {}; }
template<> __device__ __forceinline__ BytePack<0> ld_volatile_shared<0>(uint32_t addr) { return {}; }
template<> __device__ __forceinline__ void st_global<0>(uintptr_t addr, BytePack<0> value) {}
template<> __device__ __forceinline__ void st_shared<0>(uint32_t addr, BytePack<0> value) {}

// Used to define implementations for above prototypes.
#define DEFINE_ld_st(bytes, data_cxx_ty, data_ptx_ty, data_reg_ty, space, addr_cxx_ty, addr_reg_ty) \
  template<> \
  __device__ __forceinline__ BytePack<bytes> ld_##space<bytes>(addr_cxx_ty addr) { \
    data_cxx_ty tmp; \
    asm("ld." #space "." #data_ptx_ty " %0, [%1];" : "="#data_reg_ty(tmp) : #addr_reg_ty(addr) : "memory"); \
    BytePack<bytes> ans; \
    ans.native = tmp; \
    return ans; \
  } \
  template<> \
  __device__ __forceinline__ BytePack<bytes> ld_volatile_##space<bytes>(addr_cxx_ty addr) { \
    data_cxx_ty tmp; \
    asm("ld.volatile." #space "." #data_ptx_ty " %0, [%1];" : "="#data_reg_ty(tmp) : #addr_reg_ty(addr) : "memory"); \
    BytePack<bytes> ans; \
    ans.native = tmp; \
    return ans; \
  } \
  template<> \
  __device__ __forceinline__ void st_##space<bytes>(addr_cxx_ty addr, BytePack<bytes> value) { \
    data_cxx_ty tmp = value.native; \
    asm volatile("st." #space "." #data_ptx_ty " [%0], %1;" :: #addr_reg_ty(addr), #data_reg_ty(tmp) : "memory"); \
  }
// Single-byte types use 4-byte registers since there is no 1-byte register
// character for asm blocks. See https://docs.nvidia.com/cuda/inline-ptx-assembly/index.html#constraints
DEFINE_ld_st(1, uint32_t, b8, r, global, uintptr_t, l)
DEFINE_ld_st(1, uint32_t, b8, r, shared, uint32_t, r)
DEFINE_ld_st(2, uint16_t, b16, h, global, uintptr_t, l)
DEFINE_ld_st(2, uint16_t, b16, h, shared, uint32_t, r)
DEFINE_ld_st(4, uint32_t, b32, r, global, uintptr_t, l)
DEFINE_ld_st(4, uint32_t, b32, r, shared, uint32_t, r)
DEFINE_ld_st(8, uint64_t, b64, l, global, uintptr_t, l)
DEFINE_ld_st(8, uint64_t, b64, l, shared, uint32_t, r)
#undef DEFINE_ld_st

#define DEFINE_ld_st_16(space, addr_cxx_ty, addr_reg_ty) \
  template<> \
  __device__ __forceinline__ BytePack<16> ld_##space<16>(addr_cxx_ty addr) { \
    BytePack<16> ans; \
    asm("ld." #space ".v2.b64 {%0,%1}, [%2];" : "=l"(ans.u64[0]), "=l"(ans.u64[1]) : #addr_reg_ty(addr) : "memory"); \
    return ans; \
  } \
  template<> \
  __device__ __forceinline__ BytePack<16> ld_volatile_##space<16>(addr_cxx_ty addr) { \
    BytePack<16> ans; \
    asm("ld.volatile." #space ".v2.b64 {%0,%1}, [%2];" : "=l"(ans.u64[0]), "=l"(ans.u64[1]) : #addr_reg_ty(addr) : "memory"); \
    return ans; \
  } \
  template<> \
  __device__ __forceinline__ void st_##space<16>(addr_cxx_ty addr, BytePack<16> value) { \
    asm volatile("st." #space ".v2.b64 [%0], {%1,%2};" :: #addr_reg_ty(addr), "l"(value.u64[0]), "l"(value.u64[1]) : "memory"); \
  }
DEFINE_ld_st_16(global, uintptr_t, l)
DEFINE_ld_st_16(shared, uint32_t, r)
#undef DEFINE_ld_st_16

////////////////////////////////////////////////////////////////////////////////
// Atomic load/store using c++ pointers.

__device__ __forceinline__ uint64_t ld_volatile_global(uint64_t *ptr) {
  uint64_t ans;
  asm("ld.volatile.global.u64 %0, [%1];" : "=l"(ans) : "l"(cvta_to_global(ptr)) : "memory");
  return ans;
}
__device__ __forceinline__ uint64_t ld_relaxed_sys_global(uint64_t *ptr) {
  uint64_t ans;
  #if __CUDA_ARCH__ >= 700
    asm("ld.relaxed.sys.global.u64 %0, [%1];" : "=l"(ans) : "l"(cvta_to_global(ptr)) : "memory");
  #else
    asm("ld.volatile.global.u64 %0, [%1];" : "=l"(ans) : "l"(cvta_to_global(ptr)) : "memory");
  #endif
  return ans;
}
__device__ __forceinline__ uint64_t ld_acquire_sys_global(uint64_t *ptr) {
  uint64_t ans;
  #if __CUDA_ARCH__ >= 700
    asm("ld.acquire.sys.global.u64 %0, [%1];" : "=l"(ans) : "l"(cvta_to_global(ptr)) : "memory");
  #else
    asm("ld.volatile.sys.global.u64 %0, [%1]; membar.gl;" : "=l"(ans) : "l"(cvta_to_global(ptr)) : "memory");
  #endif
  return ans;
}

__device__ __forceinline__ void st_volatile_global(uint64_t *ptr, uint64_t val) {
  asm volatile("st.volatile.global.u64 [%0], %1;" :: "l"(cvta_to_global(ptr)), "l"(val) : "memory");
}
__device__ __forceinline__ void st_relaxed_sys_global(uint64_t *ptr, uint64_t val) {
  #if __CUDA_ARCH__ >= 700
    asm volatile("st.relaxed.sys.global.u64 [%0], %1;" :: "l"(cvta_to_global(ptr)), "l"(val) : "memory");
  #else
    asm volatile("st.volatile.global.u64 [%0], %1;" :: "l"(cvta_to_global(ptr)), "l"(val) : "memory");
  #endif
}
__device__ __forceinline__ void st_release_sys_global(uint64_t *ptr, uint64_t val) {
  #if __CUDA_ARCH__ >= 700
    asm volatile("st.release.sys.global.u64 [%0], %1;" :: "l"(cvta_to_global(ptr)), "l"(val) : "memory");
  #else
    asm volatile("membar.sys; st.volatile.global.u64 [%0], %1;" :: "l"(cvta_to_global(ptr)), "l"(val) : "memory");
  #endif
}

__device__ __forceinline__ void fence_acq_rel_sys() {
  #if __CUDA_ARCH__ >= 700
    asm volatile("fence.acq_rel.sys;" ::: "memory");
  #else
    asm volatile("membar.sys;" ::: "memory");
  #endif
}
__device__ __forceinline__ void fence_acq_rel_gpu() {
  #if __CUDA_ARCH__ >= 700
    asm volatile("fence.acq_rel.gpu;" ::: "memory");
  #else
    asm volatile("membar.gl;" ::: "memory");
  #endif
}

////////////////////////////////////////////////////////////////////////////////
// Multimem stores of BytePack<?>.

template<int Size>
__device__ __forceinline__ void multimem_st_global(uintptr_t addr, BytePack<Size> val);

#if __CUDA_ARCH__ >= 900 && CUDART_VERSION >= 12010
template<>
__device__ __forceinline__ void multimem_st_global<0>(uintptr_t addr, BytePack<0> val) {
  // nop
}
template<>
__device__ __forceinline__ void multimem_st_global<1>(uintptr_t addr, BytePack<1> val) {
  asm volatile("st.global.b8 [%0], %1;" :: "l"(addr), "r"((uint32_t)val.u8[0]) : "memory");
}
template<>
__device__ __forceinline__ void multimem_st_global<2>(uintptr_t addr, BytePack<2> val) {
  asm volatile("st.global.b16 [%0], %1;" :: "l"(addr), "h"(val.u16[0]) : "memory");
}
template<>
__device__ __forceinline__ void multimem_st_global<4>(uintptr_t addr, BytePack<4> val) {
  asm volatile("multimem.st.global.b32 [%0], %1;" :: "l"(addr), "r"(val.u32[0]) : "memory");
}
template<>
__device__ __forceinline__ void multimem_st_global<8>(uintptr_t addr, BytePack<8> val) {
  asm volatile("multimem.st.global.b64 [%0], %1;" :: "l"(addr), "l"(val.u64[0]) : "memory");
}
template<>
__device__ __forceinline__ void multimem_st_global<16>(uintptr_t addr, BytePack<16> val) {
  asm volatile("multimem.st.global.v4.f32 [%0], {%1,%2,%3,%4};"
    :: "l"(addr), "r"(val.u32[0]), "r"(val.u32[1]), "r"(val.u32[2]), "r"(val.u32[3])
    : "memory");
}
#else
template<int Size>
__device__ __forceinline__ void multimem_st_global(uintptr_t addr, BytePack<Size> val) {
  // nop
}
#endif

#endif
