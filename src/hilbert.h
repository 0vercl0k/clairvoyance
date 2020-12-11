// Axel '0vercl0k' Souchet - December 1 2020
#pragma once
#include <utility>

namespace Hilbert {

//
// This is code that I stole from "Hacker's Delight" figure 16–8.
//

constexpr uint32_t DistanceFromCoordinates(const uint32_t X_, const uint32_t Y_,
                                           const uint32_t Order) {
  uint32_t X = X_, Y = Y_, S = 0;
  for (int Idx = Order - 1; Idx >= 0; Idx--) {
    const uint32_t Xi = (X >> Idx) & 1;
    const uint32_t Yi = (Y >> Idx) & 1;
    if (Yi == 0) {
      const uint32_t Tmp = X;
      X = Y ^ (-Xi);
      Y = Tmp ^ (-Xi);
    }
    S = 4 * S + 2 * Xi + (Xi ^ Yi);
  }

  return S;
}

constexpr std::pair<uint32_t, uint32_t>
CoordinatesFromDistance(const uint32_t Dist, const uint32_t Order) {
  uint32_t S = Dist | (0x55'55'55'55 << 2 * Order);
  const uint32_t Sr = (S >> 1) & 0x55'55'55'55;
  uint32_t Cs = ((S & 0x55'55'55'55) + Sr) ^ 0x55'55'55'55;
  Cs ^= (Cs >> 2);
  Cs ^= (Cs >> 4);
  Cs ^= (Cs >> 8);
  Cs ^= (Cs >> 16);
  const uint32_t Swap = Cs & 0x55'55'55'55;
  const uint32_t Comp = (Cs >> 1) & 0x55'55'55'55;
  uint32_t T = (S & Swap) ^ Comp;
  S ^= Sr ^ T ^ (T << 1);
  S &= (1 << (2 * Order)) - 1;
  T = (S ^ (S >> 1)) & 0x22'22'22'22;
  S ^= T ^ (T << 1);
  T = (S ^ (S >> 2)) & 0x0C'0C'0C'0C;
  S ^= T ^ (T << 2);
  T = (S ^ (S >> 4)) & 0x00'F0'00'F0;
  S ^= T ^ (T << 4);
  T = (S ^ (S >> 8)) & 0x00'00'FF'00;
  S ^= T ^ (T << 8);
  return std::make_pair(S >> 16, S & 0xff'ff);
}

constexpr uint64_t Width(const uint64_t Order) { return 1ULL << Order; }
constexpr uint64_t Height(const uint64_t Order) { return 1ULL << Order; }

constexpr uint32_t NumberPoints(const uint64_t Order) {
  const uint64_t W = Width(Order);
  const uint64_t H = Height(Order);
  return W * H;
}

} // namespace Hilbert
