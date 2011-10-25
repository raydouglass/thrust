/*
 *  Copyright 2008-2011 NVIDIA Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/*! \file copy.h
 *  \brief CUDA implementation of device-to-device copy,
 *         based on Gregory Diamos' memcpy code.
 */

#pragma once

#include <thrust/detail/config.h>

#include <thrust/pair.h>

#include <thrust/detail/type_traits.h>
#include <thrust/detail/backend/dereference.h>
#include <thrust/detail/dispatch/is_trivial_copy.h>

namespace thrust
{
namespace detail
{
namespace backend
{
namespace cuda
{
namespace block
{

namespace trivial_copy_detail
{


template<typename Size>
  inline __device__ thrust::pair<Size,Size> quotient_and_remainder(Size n, Size d)
{
  Size quotient  = n / d;
  Size remainder = n - d * quotient; 
  return thrust::make_pair(quotient,remainder);
} // end quotient_and_remainder()


// assumes the addresses dst & src are aligned to T boundaries
template<typename Context,
         typename T>
__device__ __thrust_forceinline__
void aligned_copy(Context context, T *dst, const T *src, unsigned int num_elements)
{
  for(unsigned int i = context.thread_index();
      i < num_elements;
      i += context.block_dimension())
  {
    dst[i] = src[i];
  }
} // end aligned_copy()


} // end namespace trivial_copy_detail


template <typename Context>
__device__ __thrust_forceinline__
void trivial_copy(Context context, void* destination_, const void* source_, size_t num_bytes)
{
  // reinterpret at bytes
  char* destination  = reinterpret_cast<char*>(destination_);
  const char* source = reinterpret_cast<const char*>(source_);
 
  // TODO replace this with uint64
#if THRUST_DEVICE_COMPILER != THRUST_DEVICE_COMPILER_NVCC
  typedef long long  int2;
  typedef long long uint2;
#endif // THRUST_DEVICE_COMPILER_NVCC

  // check alignment
  // XXX can we do this in three steps?
  //     1. copy until alignment is met
  //     2. go hog wild
  //     3. get the remainder
  if(reinterpret_cast<size_t>(destination) % sizeof(uint2) != 0 || reinterpret_cast<size_t>(source) % sizeof(uint2) != 0)
  {
    for(unsigned int i = context.thread_index(); i < num_bytes; i += context.block_dimension())
    {
      destination[i] = source[i];
    }
  }
  else
  {
    // it's aligned; do a wide copy

    // this pair stores the number of int2s in the aligned portion of the arrays
    // and the number of bytes in the remainder
    const thrust::pair<size_t,size_t> num_wide_elements_and_remainder_bytes = trivial_copy_detail::quotient_and_remainder(num_bytes, sizeof(int2));

    // copy int2 elements
    trivial_copy_detail::aligned_copy(context,
                                      reinterpret_cast<int2*>(destination),
                                      reinterpret_cast<const int2*>(source),
                                      num_wide_elements_and_remainder_bytes.first);

    // XXX we could copy int elements here

    // copy remainder byte by byte

    // to find the beginning of the remainder arrays, we need to point at the beginning, and then skip the number of bytes in the aligned portion
    // this is sizeof(int2) times the number of int2s comprising the aligned portion
    const char *remainder_first  = reinterpret_cast<const char*>(source + sizeof(int2) * num_wide_elements_and_remainder_bytes.first);
          char *remainder_result = reinterpret_cast<char*>(destination  + sizeof(int2) * num_wide_elements_and_remainder_bytes.first);

    trivial_copy_detail::aligned_copy(context, remainder_result, remainder_first, num_wide_elements_and_remainder_bytes.second);
  }
} // end trivial_copy()


namespace detail
{
namespace dispatch
{

template<typename Context,
         typename RandomAccessIterator1,
         typename RandomAccessIterator2>
  __thrust_forceinline__ __device__
  RandomAccessIterator2 copy(Context context,
                             RandomAccessIterator1 first,
                             RandomAccessIterator1 last,
                             RandomAccessIterator2 result,
                             thrust::detail::true_type is_trivial_copy)
{
  typedef typename thrust::iterator_value<RandomAccessIterator1>::type T;

  // XXX these aren't working at the moment
  //const T *src = thrust::raw_pointer_cast(&*first);
  //      T *dst = thrust::raw_pointer_cast(&*result);
  const T *src = &dereference(first);
        T *dst = &dereference(result);

  size_t n = (last - first);
  thrust::detail::backend::cuda::block::trivial_copy(context, dst, src, n * sizeof(T));
  return result + n;
} // end copy()

template<typename Context,
         typename RandomAccessIterator1,
         typename RandomAccessIterator2>
  __thrust_forceinline__ __device__
  RandomAccessIterator2 copy(Context context, 
                             RandomAccessIterator1 first,
                             RandomAccessIterator1 last,
                             RandomAccessIterator2 result,
                             thrust::detail::false_type is_trivial_copy)
{
  RandomAccessIterator2 end_of_output = result + (last - first);
  
  // advance iterators
  first  += context.thread_index();
  result += context.thread_index();

  for(;
      first < last;
      first  += context.block_dimension(),
      result += context.block_dimension())
  {
    dereference(result) = dereference(first);
  } // end for

  return end_of_output;
} // end copy()

} // end namespace dispatch
} // end namespace detail

template<typename Context, 
         typename RandomAccessIterator1,
         typename RandomAccessIterator2>
  __thrust_forceinline__ __device__
  RandomAccessIterator2 copy(Context context,
                             RandomAccessIterator1 first,
                             RandomAccessIterator1 last,
                             RandomAccessIterator2 result)
{
  return detail::dispatch::copy(context, first, last, result,
#if __CUDA_ARCH__ < 200
      // does not work reliably on pre-Fermi due to "Warning: ... assuming global memory space" issues
      false_type()
#else
      typename thrust::detail::dispatch::is_trivial_copy<RandomAccessIterator1,RandomAccessIterator2>::type()
#endif
      );
} // end copy()

} // end namespace block
} // end namespace cuda
} // end namespace backend
} // end namespace detail
} // end namespace thrust

