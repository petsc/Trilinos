// @HEADER
// ***********************************************************************
// 
//                           Stokhos Package
//                 Copyright (2009) Sandia Corporation
// 
// Under terms of Contract DE-AC04-94AL85000, there is a non-exclusive
// license for use of this work by or on behalf of the U.S. Government.
// 
// This library is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; either version 2.1 of the
// License, or (at your option) any later version.
//  
// This library is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//  
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
// USA
// Questions? Contact Eric T. Phipps (etphipp@sandia.gov).
// 
// ***********************************************************************
// @HEADER
#include "Teuchos_UnitTestHarness.hpp"
#include "Teuchos_TestingHelpers.hpp"
#include "Teuchos_UnitTestRepository.hpp"
#include "Teuchos_GlobalMPISession.hpp"

#include "Teuchos_Array.hpp"
#include "Stokhos_Sacado.hpp"
#include "Sacado_Fad_DFad.hpp"
#include "Sacado_mpl_apply.hpp"
#include "Sacado_Random.hpp"

using Teuchos::RCP;
using Teuchos::rcp;

// Common setup for unit tests
template <typename VecType, typename FadType>
struct UnitTestSetup {
  int sz;

  typedef Teuchos::ValueTypeSerializer<int, VecType> VecSerializerT;
  RCP<VecSerializerT> vec_serializer;

  typedef typename Sacado::mpl::apply<FadType,VecType>::type FadVecType;
  typedef Teuchos::ValueTypeSerializer<int, FadVecType> FadVecSerializerT;
  RCP<FadVecSerializerT> fad_vec_serializer;

  UnitTestSetup() {
    sz = 100;

    // Serializers
    vec_serializer = 
      rcp(new VecSerializerT(
	    rcp(new Teuchos::ValueTypeSerializer<int,double>())));
    fad_vec_serializer = rcp(new FadVecSerializerT(vec_serializer));
  }
};

template <typename VecType>
bool testSerialization(const Teuchos::Array<VecType>& x, 
		       const std::string& tag,
		       Teuchos::FancyOStream& out) {
  
  typedef int Ordinal;
  typedef Teuchos::SerializationTraits<Ordinal,VecType> SerT;
  
  // Serialize
  Ordinal count = x.size();
  Ordinal bytes = SerT::fromCountToIndirectBytes(count, &x[0]);
  char *charBuffer = new char[bytes];
  SerT::serialize(count, &x[0], bytes, charBuffer);
  Ordinal count2 = SerT::fromIndirectBytesToCount(bytes, charBuffer);

  // Check counts match
  bool success = (count == count2);
  out << tag << " serialize/deserialize count test";
  if (success)
    out << " passed";
  else
    out << " failed";
  out << ":  \n\tExpected:  " << count << ", \n\tGot:       " << count2 << "." 
      << std::endl;

  // Deserialize
  Teuchos::Array<VecType> x2(count2);
  for (Ordinal i=0; i<count2; i++)
    x2[i].reset(x[i].size());
  SerT::deserialize(bytes, charBuffer, count2, &x2[0]);

  delete [] charBuffer;
  
  // Check coefficients match
  for (Ordinal i=0; i<count; i++) {
    bool success2 = Sacado::IsEqual<VecType>::eval(x[i], x2[i]);
    out << tag << " serialize/deserialize vec test " << i;
    if (success2)
      out << " passed";
    else
	out << " failed";
    out << ":  \n\tExpected:  " << x[i] << ", \n\tGot:       " << x2[i] 
	<< "." << std::endl;
    success = success && success2;
  }

  return success;
}

template <typename VecType, typename Serializer>
bool testSerialization(const Teuchos::Array<VecType>& x, 
		       const Serializer& serializer,
		       const std::string& tag,
		       Teuchos::FancyOStream& out) {
  
  typedef int Ordinal;
  
  // Serialize
  Ordinal count = x.size();
  Ordinal bytes = serializer.fromCountToIndirectBytes(count, &x[0]);
  char *charBuffer = new char[bytes];
  serializer.serialize(count, &x[0], bytes, charBuffer);
  
  // Deserialize
  Ordinal count2 = serializer.fromIndirectBytesToCount(bytes, charBuffer);
  Teuchos::Array<VecType> x2(count2);
  serializer.deserialize(bytes, charBuffer, count2, &x2[0]);

  delete [] charBuffer;
  
  // Check counts match
  bool success = (count == count2);
  out << tag << " serialize/deserialize count test";
  if (success)
    out << " passed";
  else
    out << " failed";
  out << ":  \n\tExpected:  " << count << ", \n\tGot:       " << count2 << "." 
      << std::endl;
  
  // Check coefficients match
  for (Ordinal i=0; i<count; i++) {
    bool success2 = Sacado::IsEqual<VecType>::eval(x[i], x2[i]);
    out << tag << " serialize/deserialize vec test " << i;
    if (success2)
      out << " passed";
    else
	out << " failed";
    out << ":  \n\tExpected:  " << x[i] << ", \n\tGot:       " << x2[i] 
	<< "." << std::endl;
    success = success && success2;
  }

  return success;
}

#define VEC_SERIALIZATION_TESTS(VecType, FadType, Vec)			\
  TEUCHOS_UNIT_TEST( Vec##_SerializationTraits, Uniform ) {		\
    int n = 7;								\
    Teuchos::Array<VecType> x(n);					\
    for (int i=0; i<n; i++) {						\
      x[i] = VecType(setup.sz);						\
      for (int j=0; j<setup.sz; j++)					\
	x[i].fastAccessCoeff(j) = rnd.number();				\
    }									\
    success = testSerialization(x, std::string(#Vec) + " Uniform", out); \
  }									\
									\
  TEUCHOS_UNIT_TEST( Vec##_SerializationTraits, Empty ) {		\
    int n = 7;								\
    Teuchos::Array<VecType> x(n);					\
    for (int i=0; i<n; i++) {						\
      x[i] = rnd.number();						\
    }									\
    success = testSerialization(x, std::string(#Vec) + " Empty", out);	\
  }									\
									\
  TEUCHOS_UNIT_TEST( Vec##_SerializationTraits, Mixed ) {		\
    int n = 6;								\
    int p[] = { 5, 0, 8, 8, 3, 0 };					\
    Teuchos::Array<VecType> x(n);					\
    for (int i=0; i<n; i++) {						\
      x[i] = VecType(p[i]);						\
      for (int j=0; j<p[i]; j++)					\
	x[i].fastAccessCoeff(j) = rnd.number();				\
    }									\
    success = testSerialization(x, std::string(#Vec) + " Mixed", out);	\
  }									\
									\
  TEUCHOS_UNIT_TEST( Vec##_Serialization, Uniform ) {			\
    int n = 7;								\
    Teuchos::Array<VecType> x(n);					\
    for (int i=0; i<n; i++) {						\
      x[i] = VecType(setup.sz);						\
      for (int j=0; j<setup.sz; j++)					\
	x[i].fastAccessCoeff(j) = rnd.number();				\
    }									\
    success = testSerialization(x, *setup.vec_serializer,		\
				std::string(#Vec) + " Uniform", out);	\
  }									\
									\
  TEUCHOS_UNIT_TEST( Vec##_Serialization, Empty ) {			\
    int n = 7;								\
    Teuchos::Array<VecType> x(n);					\
    for (int i=0; i<n; i++) {						\
      x[i] = VecType(1);						\
      x[i].val() = rnd.number();					\
    }									\
    success = testSerialization(x, *setup.vec_serializer,		\
				std::string(#Vec) + " Empty", out);	\
  }									\
									\
  TEUCHOS_UNIT_TEST( Vec##_Serialization, Mixed ) {			\
    int n = 6;								\
    int p[] = { 5, 0, 8, 8, 3, 0 };					\
    Teuchos::Array<VecType> x(n);					\
    for (int i=0; i<n; i++) {						\
      x[i] = VecType(p[i]);						\
      for (int j=0; j<p[i]; j++)					\
	x[i].fastAccessCoeff(j) = rnd.number();				\
    }									\
    success = testSerialization(x, *setup.vec_serializer,		\
				std::string(#Vec) + " Mixed", out);	\
  } 									\
  TEUCHOS_UNIT_TEST( Vec##_Serialization, FadVecUniform ) {		\
    typedef Sacado::mpl::apply<FadType,VecType>::type FadVecType;	\
    int n = 7;								\
    int p = 3;								\
    Teuchos::Array<FadVecType> x(n);					\
    for (int i=0; i<n; i++) {						\
      VecType f(setup.sz);						\
      for (int k=0; k<setup.sz; k++)					\
	f.fastAccessCoeff(k) = rnd.number();				\
      x[i] = FadVecType(p, f);						\
      for (int j=0; j<p; j++) {						\
	VecType g(setup.sz);						\
	for (int k=0; k<setup.sz; k++)					\
	  g.fastAccessCoeff(k) = rnd.number();				\
	x[i].fastAccessDx(j) = g;					\
      }									\
    }									\
    success =								\
      testSerialization(x, *setup.fad_vec_serializer,			\
			std::string(#Vec) + " Nested Uniform", out);	\
  }									\
  TEUCHOS_UNIT_TEST( Vec##_Serialization, FadVecEmptyInner ) {		\
    typedef Sacado::mpl::apply<FadType,VecType>::type FadVecType;	\
    int n = 7;								\
    int p = 3;								\
    Teuchos::Array<FadVecType> x(n);					\
    for (int i=0; i<n; i++) {						\
      VecType f(setup.sz);						\
      for (int k=0; k<setup.sz; k++)					\
	f.fastAccessCoeff(k) = rnd.number();				\
      x[i] = FadVecType(p, f);						\
      for (int j=0; j<p; j++)						\
	x[i].fastAccessDx(j) =  rnd.number();				\
    }									\
    success =								\
      testSerialization(						\
	x, *setup.fad_vec_serializer,					\
	std::string(#Vec) + " Nested Empty Inner", out);		\
  }									\
  TEUCHOS_UNIT_TEST( Vec##_Serialization, FadVecEmptyOuter ) {		\
    typedef Sacado::mpl::apply<FadType,VecType>::type FadVecType;	\
    int n = 7;								\
    Teuchos::Array<FadVecType> x(n);					\
    for (int i=0; i<n; i++) {						\
      VecType f(setup.sz);						\
      for (int k=0; k<setup.sz; k++)					\
	f.fastAccessCoeff(k) = rnd.number();				\
      x[i] = FadVecType(f);						\
    }									\
    success =								\
      testSerialization(						\
	x, *setup.fad_vec_serializer,					\
	std::string(#Vec) + " Nested Empty Outer", out);		\
  }									\
  TEUCHOS_UNIT_TEST( Vec##_Serialization, FadVecEmptyAll ) {		\
    typedef Sacado::mpl::apply<FadType,VecType>::type FadVecType;	\
    int n = 7;								\
    Teuchos::Array<FadVecType> x(n);					\
    for (int i=0; i<n; i++) {						\
      x[i] = rnd.number();						\
    }									\
    success =								\
      testSerialization(						\
	x, *setup.fad_vec_serializer,					\
	std::string(#Vec) + " Nested Empty All", out);			\
  }

typedef Stokhos::StandardStorage<int,double> storage_type;
typedef Sacado::Fad::DFad<double> fad_type;
namespace VecTest {
  Sacado::Random<double> rnd;
  typedef Sacado::ETV::Vector<double,storage_type> vec_type;
  UnitTestSetup<vec_type, fad_type> setup;
  VEC_SERIALIZATION_TESTS(vec_type, fad_type, OrthogPoly)
}

int main( int argc, char* argv[] ) {
  Teuchos::GlobalMPISession mpiSession(&argc, &argv);
  return Teuchos::UnitTestRepository::runUnitTestsFromMain(argc, argv);
}