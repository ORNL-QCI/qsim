// Copyright 2019 Google LLC. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef STATESPACE_SSE_H_
#define STATESPACE_SSE_H_

#include <smmintrin.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <functional>

#include "statespace.h"
#include "util.h"

namespace qsim {

namespace detail {

inline __m128i GetZeroMaskSSE(uint64_t i, uint64_t mask, uint64_t bits) {
  __m128i s1 = _mm_set_epi64x(i + 2, i + 0);
  __m128i s2 = _mm_set_epi64x(i + 3, i + 1);
  __m128i ma = _mm_set1_epi64x(mask);
  __m128i bi = _mm_set1_epi64x(bits);

  s1 = _mm_and_si128(s1, ma);
  s2 = _mm_and_si128(s2, ma);

  s1 = _mm_cmpeq_epi64(s1, bi);
  s2 = _mm_cmpeq_epi64(s2, bi);

  return _mm_blend_epi16(s1, s2, 204);  // 11001100
}

inline double HorizontalSumSSE(__m128 s) {
  float buf[4];
  _mm_storeu_ps(buf, s);
  return buf[0] + buf[1] + buf[2] + buf[3];
}

}  // namespace detail

/**
 * Object containing context and routines for SSE state-vector manipulations.
 * State is a vectorized sequence of four real components followed by four
 * imaginary components. Four single-precison floating numbers can be loaded
 * into an SSE register.
 */
template <typename For>
class StateSpaceSSE : public StateSpace<StateSpaceSSE<For>, For, float> {
 private:
  using Base = StateSpace<StateSpaceSSE<For>, For, float>;

 public:
  using State = typename Base::State;
  using fp_type = typename Base::fp_type;

  template <typename... ForArgs>
  explicit StateSpaceSSE(unsigned num_qubits, ForArgs&&... args)
      : Base(MinimumRawSize(2 * (uint64_t{1} << num_qubits)),
             num_qubits, args...) {}

  static uint64_t MinimumRawSize(uint64_t raw_size) {
    return std::max(uint64_t{8}, raw_size);
  }

  bool InternalToNormalOrder(State& state) const {
    if (state.size() != Base::raw_size_) {
      return false;
    }

    if (Base::num_qubits_ == 1) {
      auto s = state.get();

      s[2] = s[1];
      s[1] = s[4];
      s[3] = s[5];

      for (uint64_t i = 4; i < 8; ++i) {
        s[i] = 0;
      }
    } else {
      auto f = [](unsigned n, unsigned m, uint64_t i, fp_type* p) {
        auto s = p + 8 * i;

        fp_type re[3];
        fp_type im[3];

        for (uint64_t i = 0; i < 3; ++i) {
          re[i] = s[i + 1];
          im[i] = s[i + 4];
        }

        for (uint64_t i = 0; i < 3; ++i) {
          s[2 * i + 1] = im[i];
          s[2 * i + 2] = re[i];
        }
      };

      Base::for_.Run(Base::raw_size_ / 8, f, state.get());
    }

    return true;
  }

  bool NormalToInternalOrder(State& state) const {
    if (state.size() != Base::raw_size_) {
      return false;
    }

    if (Base::num_qubits_ == 1) {
      auto s = state.get();

      s[4] = s[1];
      s[1] = s[2];
      s[5] = s[3];

      s[2] = 0;
      s[3] = 0;
      s[6] = 0;
      s[7] = 0;
    } else {
      auto f = [](unsigned n, unsigned m, uint64_t i, fp_type* p) {
        auto s = p + 8 * i;

        fp_type re[3];
        fp_type im[3];

        for (uint64_t i = 0; i < 3; ++i) {
          im[i] = s[2 * i + 1];
          re[i] = s[2 * i + 2];
        }

        for (uint64_t i = 0; i < 3; ++i) {
          s[i + 1] = re[i];
          s[i + 4] = im[i];
        }
      };

      Base::for_.Run(Base::raw_size_ / 8, f, state.get());
    }

    return true;
  }

  bool SetAllZeros(State& state) const {
    if (state.size() != Base::raw_size_) {
      return false;
    }

    __m128 val0 = _mm_setzero_ps();

    auto f = [](unsigned n, unsigned m, uint64_t i, __m128 val0, fp_type* p) {
      _mm_store_ps(p + 8 * i, val0);
      _mm_store_ps(p + 8 * i + 4, val0);
    };

    Base::for_.Run(Base::raw_size_ / 8, f, val0, state.get());

    return true;
  }

  // Uniform superposition.
  bool SetStateUniform(State& state) const {
    if (state.size() != Base::raw_size_) {
      return false;
    }

    __m128 val0 = _mm_setzero_ps();
    __m128 valu;

    fp_type v = double{1} / std::sqrt(Base::Size());

    if (Base::num_qubits_ == 1) {
      valu = _mm_set_ps(0, 0, v, v);
    } else {
      valu = _mm_set1_ps(v);
    }

    auto f = [](unsigned n, unsigned m, uint64_t i,
                __m128 val0, __m128 valu, fp_type* p) {
      _mm_store_ps(p + 8 * i, valu);
      _mm_store_ps(p + 8 * i + 4, val0);
    };

    Base::for_.Run(Base::raw_size_ / 8, f, val0, valu, state.get());

    return true;
  }

  // |0> state.
  bool SetStateZero(State& state) const {
    if (state.size() != Base::raw_size_) {
      return false;
    }

    SetAllZeros(state);
    state.get()[0] = 1;

    return true;
  }

  static std::complex<fp_type> GetAmpl(const State& state, uint64_t i) {
    uint64_t p = (8 * (i / 4)) + (i % 4);
    return std::complex<fp_type>(state.get()[p], state.get()[p + 4]);
  }

  static void SetAmpl(
      State& state, uint64_t i, const std::complex<fp_type>& ampl) {
    uint64_t p = (8 * (i / 4)) + (i % 4);
    state.get()[p] = std::real(ampl);
    state.get()[p + 4] = std::imag(ampl);
  }

  static void SetAmpl(State& state, uint64_t i, fp_type re, fp_type im) {
    uint64_t p = (8 * (i / 4)) + (i % 4);
    state.get()[p] = re;
    state.get()[p + 4] = im;
  }

  // Does the equivalent of dest += src elementwise.
  bool AddState(const State& src, State& dest) const {
    if (src.size() != Base::raw_size_ || dest.size() != Base::raw_size_) {
      return false;
    }

    auto f = [](unsigned n, unsigned m, uint64_t i,
                const fp_type* p1, fp_type* p2) {
      __m128 re1 = _mm_load_ps(p1 + 8 * i);
      __m128 im1 = _mm_load_ps(p1 + 8 * i + 4);
      __m128 re2 = _mm_load_ps(p2 + 8 * i);
      __m128 im2 = _mm_load_ps(p2 + 8 * i + 4);

      _mm_store_ps(p2 + 8 * i, _mm_add_ps(re1, re2));
      _mm_store_ps(p2 + 8 * i + 4, _mm_add_ps(im1, im2));
    };

    Base::for_.Run(Base::raw_size_ / 8, f, src.get(), dest.get());

    return true;
  }

  // Does the equivalent of state *= a elementwise.
  bool Multiply(fp_type a, State& state) const {
    if (state.size() != Base::raw_size_) {
      return false;
    }

    __m128 r = _mm_set1_ps(a);

    auto f = [](unsigned n, unsigned m, uint64_t i, __m128 r, fp_type* p) {
      __m128 re = _mm_load_ps(p + 8 * i);
      __m128 im = _mm_load_ps(p + 8 * i + 4);

      re = _mm_mul_ps(re, r);
      im = _mm_mul_ps(im, r);

      _mm_store_ps(p + 8 * i, re);
      _mm_store_ps(p + 8 * i + 4, im);
    };

    Base::for_.Run(Base::raw_size_ / 8, f, r, state.get());

    return true;
  }

  std::complex<double> InnerProduct(
      const State& state1, const State& state2) const {
    if (state1.size() != Base::raw_size_ || state2.size() != Base::raw_size_) {
      return std::nan("");
    }

    auto f = [](unsigned n, unsigned m, uint64_t i,
                const fp_type* p1, const fp_type* p2) -> std::complex<double> {
      __m128 re1 = _mm_load_ps(p1 + 8 * i);
      __m128 im1 = _mm_load_ps(p1 + 8 * i + 4);
      __m128 re2 = _mm_load_ps(p2 + 8 * i);
      __m128 im2 = _mm_load_ps(p2 + 8 * i + 4);

      __m128 ip_re = _mm_add_ps(_mm_mul_ps(re1, re2), _mm_mul_ps(im1, im2));
      __m128 ip_im = _mm_sub_ps(_mm_mul_ps(re1, im2), _mm_mul_ps(im1, re2));

      double re = detail::HorizontalSumSSE(ip_re);
      double im = detail::HorizontalSumSSE(ip_im);

      return std::complex<double>{re, im};
    };

    using Op = std::plus<std::complex<double>>;
    return Base::for_.RunReduce(
        Base::raw_size_ / 8, f, Op(), state1.get(), state2.get());
  }

  double RealInnerProduct(const State& state1, const State& state2) const {
    if (state1.size() != Base::raw_size_ || state2.size() != Base::raw_size_) {
      return std::nan("");
    }

    auto f = [](unsigned n, unsigned m, uint64_t i,
                const fp_type* p1, const fp_type* p2) -> double {
      __m128 re1 = _mm_load_ps(p1 + 8 * i);
      __m128 im1 = _mm_load_ps(p1 + 8 * i + 4);
      __m128 re2 = _mm_load_ps(p2 + 8 * i);
      __m128 im2 = _mm_load_ps(p2 + 8 * i + 4);

      __m128 ip_re = _mm_add_ps(_mm_mul_ps(re1, re2), _mm_mul_ps(im1, im2));

      return detail::HorizontalSumSSE(ip_re);
    };

    using Op = std::plus<double>;
    return Base::for_.RunReduce(
        Base::raw_size_ / 8, f, Op(), state1.get(), state2.get());
  }

  template <typename DistrRealType = double>
  std::vector<uint64_t> Sample(
      const State& state, uint64_t num_samples, unsigned seed) const {
    std::vector<uint64_t> bitstrings;

    if (state.size() == Base::raw_size_ && num_samples > 0) {
      double norm = 0;
      uint64_t size = Base::raw_size_ / 8;
      const fp_type* p = state.get();

      for (uint64_t k = 0; k < size; ++k) {
        for (unsigned j = 0; j < 4; ++j) {
          auto re = p[8 * k + j];
          auto im = p[8 * k + 4 + j];
          norm += re * re + im * im;
        }
      }

      auto rs = GenerateRandomValues<DistrRealType>(num_samples, seed, norm);

      uint64_t m = 0;
      double csum = 0;
      bitstrings.reserve(num_samples);

      for (uint64_t k = 0; k < size; ++k) {
        for (unsigned j = 0; j < 4; ++j) {
          auto re = p[8 * k + j];
          auto im = p[8 * k + 4 + j];
          csum += re * re + im * im;
          while (rs[m] < csum && m < num_samples) {
            bitstrings.emplace_back(4 * k + j);
            ++m;
          }
        }
      }
    }

    return bitstrings;
  }

  using MeasurementResult = typename Base::MeasurementResult;

  bool CollapseState(const MeasurementResult& mr, State& state) const {
    if (state.size() != Base::raw_size_) {
      return false;
    }

    __m128 zero = _mm_set1_ps(0);

    auto f1 = [](unsigned n, unsigned m, uint64_t i, uint64_t mask,
                 uint64_t bits, __m128 zero, const fp_type* p) -> double {
      __m128 ml = _mm_castsi128_ps(detail::GetZeroMaskSSE(4 * i, mask, bits));

      __m128 re = _mm_load_ps(p + 8 * i);
      __m128 im = _mm_load_ps(p + 8 * i + 4);
      __m128 s1 = _mm_add_ps(_mm_mul_ps(re, re), _mm_mul_ps(im, im));

      s1 = _mm_blendv_ps(zero, s1, ml);

      return detail::HorizontalSumSSE(s1);
    };

    using Op = std::plus<double>;
    double norm = Base::for_.RunReduce(Base::raw_size_ / 8, f1, Op(),
                                       mr.mask, mr.bits, zero, state.get());

    __m128 renorm = _mm_set1_ps(1.0 / std::sqrt(norm));

    auto f2 = [](unsigned n, unsigned m, uint64_t i, uint64_t mask,
                 uint64_t bits, __m128 renorm, __m128 zero, fp_type* p) {
      __m128 ml = _mm_castsi128_ps(detail::GetZeroMaskSSE(4 * i, mask, bits));

      __m128 re = _mm_load_ps(p + 8 * i);
      __m128 im = _mm_load_ps(p + 8 * i + 4);

      re = _mm_blendv_ps(zero, _mm_mul_ps(re, renorm), ml);
      im = _mm_blendv_ps(zero, _mm_mul_ps(im, renorm), ml);

      _mm_store_ps(p + 8 * i, re);
      _mm_store_ps(p + 8 * i + 4, im);
    };

    Base::for_.Run(
        Base::raw_size_ / 8, f2, mr.mask, mr.bits, renorm, zero, state.get());

    return true;
  }

  std::vector<double> PartialNorms(const State& state) const {
    if (state.size() != Base::raw_size_) {
      return {};
    }

    auto f = [](unsigned n, unsigned m, uint64_t i,
                const fp_type* p) -> double {
      __m128 re = _mm_load_ps(p + 8 * i);
      __m128 im = _mm_load_ps(p + 8 * i + 4);
      __m128 s1 = _mm_add_ps(_mm_mul_ps(re, re), _mm_mul_ps(im, im));

      return detail::HorizontalSumSSE(s1);
    };

    using Op = std::plus<double>;
    return Base::for_.RunReduceP(Base::raw_size_ / 8, f, Op(), state.get());
  }

  uint64_t FindMeasuredBits(
      unsigned m, double r, uint64_t mask, const State& state) const {
    if (state.size() != Base::raw_size_) {
      return -1;
    }

    double csum = 0;

    uint64_t k0 = Base::for_.GetIndex0(Base::raw_size_ / 8, m);
    uint64_t k1 = Base::for_.GetIndex1(Base::raw_size_ / 8, m);

    const fp_type* p = state.get();

    for (uint64_t k = k0; k < k1; ++k) {
      for (uint64_t j = 0; j < 4; ++j) {
        auto re = p[8 * k + j];
        auto im = p[8 * k + 4 + j];
        csum += re * re + im * im;
        if (r < csum) {
          return (4 * k + j) & mask;
        }
      }
    }

    return -1;
  }
};

}  // namespace qsim

#endif  // STATESPACE_SSE_H_
