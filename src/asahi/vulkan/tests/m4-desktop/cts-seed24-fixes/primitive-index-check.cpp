#include "tcuVector.hpp"
#include <set>
#include <vector>
#include <functional>
#include <algorithm>
#include <random>
#include <limits>
#include <cassert>
#include <cstdio>
struct PerPrimitive
{
	deInt32		patchPrimitiveID;	//!< gl_PrimitiveID in tessellation evaluation shader
	deInt32		primitiveID;		//!< ID of an output primitive in geometry shader (user-defined)

	deInt32		unused_padding[2];

	tcu::Vec4	tessCoord[3];		//!< 3 coords for triangles/quads, 2 for isolines, 1 for point mode. Vec4 due to alignment.
};

typedef std::vector<PerPrimitive> PerPrimitiveVec;

template <typename SeqT, int Size, typename Pred>
class LexCompare
{
public:
	LexCompare (void) : m_pred(Pred()) {}

	bool operator() (const SeqT& a, const SeqT& b) const
	{
		for (int i = 0; i < Size; ++i)
		{
			if (m_pred(a[i], b[i]))
				return true;
			if (m_pred(b[i], a[i]))
				return false;
		}
		return false;
	}

private:
	Pred m_pred;
};

template <int Size>
class VecLexLessThan : public LexCompare<tcu::Vector<float, Size>, Size, std::less<float> >
{
};

bool originalCompare (const PerPrimitive* const primitivesA, const PerPrimitive* const primitivesB, const int numPrimitivesPerPatch)
{
	int ndxB = 0;
	for (int ndxA = 0; ndxA < numPrimitivesPerPatch; ++ndxA)
	{
		const tcu::Vec4 (&coordsA)[3] = primitivesA[ndxA].tessCoord;
		bool match = false;

		// Actually both sets are usually somewhat sorted, so don't reset ndxB after each match. Instead, continue from the next index.
		for (int i = 0; i < numPrimitivesPerPatch; ++i)
		{
			const tcu::Vec4 (&coordsB)[3] = primitivesB[ndxB].tessCoord;
			ndxB = (ndxB + 1) % numPrimitivesPerPatch;

			if (coordsA[0] == coordsB[0] && coordsA[1] == coordsB[1] && coordsA[2] == coordsB[2])
			{
				match = true;
				break;
			}
		}

		if (!match)
			return false;
	}
	return true;
}
bool comparePrimitivesExact (const PerPrimitive* const primitivesA, const PerPrimitive* const primitivesB, const int numPrimitivesPerPatch)
{
	typedef tcu::Vector<tcu::Vec4, 3> OrderedPrimitive;
	typedef LexCompare<OrderedPrimitive, 3, VecLexLessThan<4> > PrimitiveLess;
	typedef std::set<OrderedPrimitive, PrimitiveLess> PrimitiveSet;
	PrimitiveSet known;

	// Preserve the original membership comparison, including duplicate
	// invocations and vertex order. Index B once instead of scanning it for
	// every primitive of A. NaN coordinates cannot match by float equality;
	// omit them from B so the ordered index retains a strict weak ordering.
	for (int i = 0; i < numPrimitivesPerPatch; ++i)
	{
		const tcu::Vec4 (&c)[3] = primitivesB[i].tessCoord;
		if (c[0] == c[0] && c[1] == c[1] && c[2] == c[2])
			known.insert(OrderedPrimitive(c[0], c[1], c[2]));
	}
	for (int i = 0; i < numPrimitivesPerPatch; ++i)
	{
		const tcu::Vec4 (&c)[3] = primitivesA[i].tessCoord;
		if (c[0] != c[0] || c[1] != c[1] || c[2] != c[2] ||
			known.find(OrderedPrimitive(c[0], c[1], c[2])) == known.end())
			return false;
	}
	return true;
}

int main() {
 std::mt19937 rng(24); unsigned checked=0;
 for (unsigned trial=0; trial<10000; ++trial) {
  int n=rng()%100; PerPrimitiveVec a(n), b(n);
  for(auto &p:a) for(auto &v:p.tessCoord) for(int c=0;c<4;++c) v[c]=(int)(rng()%9)-4;
  b=a; std::shuffle(b.begin(),b.end(),rng);
  if(n) {
   switch(trial%6) {
   case 0: b[rng()%n].tessCoord[rng()%3][rng()%4] += 1; break;
   case 1: for(auto &p:a) p=a[0]; break;
   case 2: a[rng()%n].tessCoord[0][0]=std::numeric_limits<float>::quiet_NaN(); break;
   case 3: b[rng()%n].tessCoord[0][0]=std::numeric_limits<float>::quiet_NaN(); break;
   case 4: a[0].tessCoord[0][0]=0.f; b[0]=a[0]; b[0].tessCoord[0][0]=-0.f; break;
   default: break;
   }
  }
  if (originalCompare(a.data(),b.data(),n)!=comparePrimitivesExact(a.data(),b.data(),n)) return 1; ++checked;
 }
 printf("PASS: %u old/new primitive membership comparisons (reordered, duplicates, mismatches, NaNs, signed zero)\n",checked);
}
