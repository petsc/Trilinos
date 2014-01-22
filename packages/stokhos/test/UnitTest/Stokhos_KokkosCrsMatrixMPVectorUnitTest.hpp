// @HEADER
// ***********************************************************************
//
//                           Stokhos Package
//                 Copyright (2009) Sandia Corporation
//
// Under terms of Contract DE-AC04-94AL85000, there is a non-exclusive
// license for use of this work by or on behalf of the U.S. Government.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Eric T. Phipps (etphipp@sandia.gov).
//
// ***********************************************************************
// @HEADER

#include "Teuchos_UnitTestHelpers.hpp"
#include "Stokhos_UnitTestHelpers.hpp"

#include "Stokhos_Sacado_Kokkos.hpp"
#include "Kokkos_CrsMatrix_MP_Vector.hpp"

// For computing DeviceConfig
#include "Kokkos_hwloc.hpp"
#include "Kokkos_Cuda.hpp"

// Helper functions
template< typename IntType >
inline
IntType map_fem_graph_coord( const IntType& N,
                             const IntType& i,
                             const IntType& j,
                             const IntType& k )
{
  return k + N * ( j + N * i );
}

template < typename ordinal >
inline
ordinal generate_fem_graph( ordinal N,
                            std::vector< std::vector<ordinal> >& graph )
{
  graph.resize( N * N * N, std::vector<ordinal>() );

  ordinal total = 0;

  for ( int i = 0; i < (int) N; ++i ) {
    for ( int j = 0; j < (int) N; ++j ) {
      for ( int k = 0; k < (int) N; ++k ) {

        const ordinal row = map_fem_graph_coord((int)N,i,j,k);

        graph[row].reserve(27);

        for ( int ii = -1; ii < 2; ++ii ) {
          for ( int jj = -1; jj < 2; ++jj ) {
            for ( int kk = -1; kk < 2; ++kk ) {
              if ( 0 <= i + ii && i + ii < (int) N &&
                   0 <= j + jj && j + jj < (int) N &&
                   0 <= k + kk && k + kk < (int) N ) {
                ordinal col = map_fem_graph_coord((int)N,i+ii,j+jj,k+kk);

                graph[row].push_back(col);
              }
            }}}
        total += graph[row].size();
      }}}

  return total;
}

template <typename scalar, typename ordinal>
inline
scalar generate_matrix_coefficient( const ordinal nFEM,
                                    const ordinal nStoch,
                                    const ordinal iRowFEM,
                                    const ordinal iColFEM,
                                    const ordinal iStoch )
{
  const scalar A_fem = ( 10.0 + scalar(iRowFEM) / scalar(nFEM) ) +
    (  5.0 + scalar(iColFEM) / scalar(nFEM) );

  const scalar A_stoch = ( 1.0 + scalar(iStoch) / scalar(nStoch) );

  return A_fem + A_stoch;
  //return 1.0;
}

template <typename scalar, typename ordinal>
inline
scalar generate_vector_coefficient( const ordinal nFEM,
                                    const ordinal nStoch,
                                    const ordinal iColFEM,
                                    const ordinal iStoch )
{
  const scalar X_fem = 100.0 + scalar(iColFEM) / scalar(nFEM);
  const scalar X_stoch =  1.0 + scalar(iStoch) / scalar(nStoch);
  return X_fem + X_stoch;
  //return 1.0;
}

// Reasonable tolerances for common precisions
template <typename Scalar> struct ScalarTol {};
template <> struct ScalarTol<float> {  static float  tol() { return 1e-4;  } };
template <> struct ScalarTol<double> { static double tol() { return 1e-10; } };

// Compare two rank-2 views for equality, to given precision
template <typename array_type, typename scalar_type>
bool compare_rank_2_views(const array_type& y,
                          const array_type& y_exp,
                          const scalar_type rel_tol,
                          const scalar_type abs_tol,
                          Teuchos::FancyOStream& out)
{
  typedef typename array_type::size_type size_type;
  typename array_type::HostMirror hy = Kokkos::create_mirror_view(y);
  typename array_type::HostMirror hy_exp = Kokkos::create_mirror_view(y_exp);
  Kokkos::deep_copy(hy, y);
  Kokkos::deep_copy(hy_exp, y_exp);

  size_type num_rows = y.dimension_0();
  size_type num_cols = y.dimension_1();
  bool success = true;
  for (size_type i=0; i<num_rows; ++i) {
    for (size_type j=0; j<num_cols; ++j) {
      scalar_type diff = std::abs( hy(i,j) - hy_exp(i,j) );
      scalar_type tol = rel_tol*std::abs(hy_exp(i,j)) + abs_tol;
      bool s = diff < tol;
      out << "y_expected(" << i << "," << j << ") - "
          << "y(" << i << "," << j << ") = " << hy_exp(i,j)
          << " - " << hy(i,j) << " == "
          << diff << " < " << tol << " : ";
      if (s)
        out << "passed";
      else
        out << "failed";
      out << std::endl;
      success = success && s;
    }
  }

  return success;
}

// Helper function to build a diagonal matrix
template <typename MatrixType>
MatrixType
buildDiagonalMatrix(typename MatrixType::ordinal_type nrow,
                    typename MatrixType::ordinal_type mp_vector_size) {
  typedef typename MatrixType::ordinal_type ordinal_type;
  typedef typename MatrixType::StaticCrsGraphType matrix_graph_type;
  typedef typename MatrixType::values_type matrix_values_type;

  std::vector< std::vector<ordinal_type> > graph(nrow);
  for (ordinal_type i=0; i<nrow; ++i)
    graph[i] = std::vector<ordinal_type>(1, i);
  ordinal_type graph_length = nrow;

  matrix_graph_type matrix_graph =
    Kokkos::create_staticcrsgraph<matrix_graph_type>("graph", graph);
  matrix_values_type matrix_values =
    matrix_values_type("values", graph_length, mp_vector_size);

  MatrixType matrix("matrix", nrow, matrix_values, matrix_graph);
  return matrix;
}

//
// Tests
//

// Kernel to set diagonal of a matrix to prescribed values
template <typename MatrixType>
struct ReplaceDiagonalValuesKernel {
  typedef typename MatrixType::device_type device_type;
  typedef typename MatrixType::size_type size_type;
  typedef typename MatrixType::scalar_type scalar_type;
  typedef typename MatrixType::ordinal_type ordinal_type;

  const MatrixType m_matrix;
  ReplaceDiagonalValuesKernel(const MatrixType matrix) : m_matrix(matrix) {};

  // Replace diagonal entry for row 'i' with a value
  KOKKOS_INLINE_FUNCTION
  void operator() (const size_type i) const {
    const ordinal_type row = i;
    const ordinal_type col = i;
    scalar_type val = scalar_type(row);
    m_matrix.replaceValues(row, &col, 1, &val, true);
  }

  // Kernel launch
  static void apply(const MatrixType matrix) {
    const size_type nrow = matrix.numRows();
    Kokkos::parallel_for( nrow, ReplaceDiagonalValuesKernel(matrix) );
  }

  // Check the result is as expected
  static bool check(const MatrixType matrix,
                    Teuchos::FancyOStream& out) {
    typedef typename MatrixType::values_type matrix_values_type;
    typename matrix_values_type::HostMirror host_matrix_values =
      Kokkos::create_mirror_view(matrix.values);
    Kokkos::deep_copy(host_matrix_values, matrix.values);
    const ordinal_type nrow = matrix.numRows();
    bool success = true;
    for (ordinal_type row=0; row<nrow; ++row) {
      bool s = compareVecs(host_matrix_values(row),
                           "matrix_values(row)",
                           scalar_type(row),
                           "scalar_type(row)",
                           0.0, 0.0, out);
      success = success && s;
    }
    return success;
  }
};

// Kernel to add values to the diagonal of a matrix
template <typename MatrixType>
struct AddDiagonalValuesKernel {
  typedef typename MatrixType::device_type device_type;
  typedef typename MatrixType::size_type size_type;
  typedef typename MatrixType::scalar_type scalar_type;
  typedef typename MatrixType::ordinal_type ordinal_type;

  const MatrixType m_matrix;
  AddDiagonalValuesKernel(const MatrixType matrix) : m_matrix(matrix) {};

  // Replace diagonal entry for row 'i' with a value
  KOKKOS_INLINE_FUNCTION
  void operator() (const size_type i) const {
    const ordinal_type row = i;
    const ordinal_type col = i;
    scalar_type val = scalar_type(row);
    m_matrix.sumIntoValues(row, &col, 1, &val, true);
  }

  // Kernel launch
  static void apply(const MatrixType matrix) {
    const size_type nrow = matrix.numRows();
    Kokkos::parallel_for( nrow, AddDiagonalValuesKernel(matrix) );
  }

  // Check the result is as expected
  static bool check(const MatrixType matrix,
                    Teuchos::FancyOStream& out) {
    typedef typename MatrixType::values_type matrix_values_type;
    typename matrix_values_type::HostMirror host_matrix_values =
      Kokkos::create_mirror_view(matrix.values);
    Kokkos::deep_copy(host_matrix_values, matrix.values);
    const ordinal_type nrow = matrix.numRows();
    bool success = true;
    for (ordinal_type row=0; row<nrow; ++row) {
      bool s = compareVecs(host_matrix_values(row),
                           "matrix_values(row)",
                           scalar_type(row),
                           "scalar_type(row)",
                           0.0, 0.0, out);
      success = success && s;
    }
    return success;
  }
};

// Kernel to add values to the diagonal of a matrix where each thread
// adds to the same row (checks atomic really works)
template <typename MatrixType>
struct AddDiagonalValuesAtomicKernel {
  typedef typename MatrixType::device_type device_type;
  typedef typename MatrixType::size_type size_type;
  typedef typename MatrixType::scalar_type scalar_type;
  typedef typename MatrixType::ordinal_type ordinal_type;

  const MatrixType m_matrix;
  AddDiagonalValuesAtomicKernel(const MatrixType matrix) : m_matrix(matrix) {};

  // Replace diagonal entry for row 'i' with a value
  KOKKOS_INLINE_FUNCTION
  void operator() (const size_type i) const {
    const ordinal_type row = 0;
    const ordinal_type col = 0;
    scalar_type val = scalar_type(i);
    m_matrix.sumIntoValues(row, &col, 1, &val, true);
  }

  // Kernel launch
  static void apply(const MatrixType matrix) {
    const size_type nrow = matrix.numRows();
    Kokkos::parallel_for( nrow, AddDiagonalValuesAtomicKernel(matrix) );
  }

  // Check the result is as expected
  static bool check(const MatrixType matrix,
                    Teuchos::FancyOStream& out) {
    typedef typename MatrixType::values_type matrix_values_type;
    typename matrix_values_type::HostMirror host_matrix_values =
      Kokkos::create_mirror_view(matrix.values);
    Kokkos::deep_copy(host_matrix_values, matrix.values);
    const ordinal_type nrow = matrix.numRows();
    bool success = true;
    for (ordinal_type row=0; row<nrow; ++row) {
      scalar_type val;
      if (row == 0)
        val = scalar_type( nrow*(nrow-1)/2 );
      else
        val = scalar_type(0.0);
      bool s = compareVecs(host_matrix_values(row),
                           "matrix_values(row)",
                           val,
                           "val",
                           0.0, 0.0, out);
      success = success && s;
    }
    return success;
  }
};

TEUCHOS_UNIT_TEST_TEMPLATE_3_DECL(
  Kokkos_CrsMatrix_MP, ReplaceValues, Scalar, Ordinal, Device )
{
  const Ordinal VectorSize = 3;
  typedef Stokhos::StaticFixedStorage<Ordinal,Scalar,VectorSize,Device> Storage;
  typedef Sacado::MP::Vector<Storage> MatrixScalar;
  typedef Kokkos::CrsMatrix<MatrixScalar,Ordinal,Device> Matrix;

  // Build diagonal matrix
  Ordinal nrow = 10;
  Matrix matrix = buildDiagonalMatrix<Matrix>(nrow, VectorSize);

  // Launch our kernel
  typedef ReplaceDiagonalValuesKernel<Matrix> kernel;
  kernel::apply(matrix);

  // Check the result
  success = kernel::check(matrix, out);
}

TEUCHOS_UNIT_TEST_TEMPLATE_3_DECL(
  Kokkos_CrsMatrix_MP, SumIntoValues, Scalar, Ordinal, Device )
{
  const Ordinal VectorSize = 3;
  typedef Stokhos::StaticFixedStorage<Ordinal,Scalar,VectorSize,Device> Storage;
  typedef Sacado::MP::Vector<Storage> MatrixScalar;
  typedef Kokkos::CrsMatrix<MatrixScalar,Ordinal,Device> Matrix;

  // Build diagonal matrix
  Ordinal nrow = 10;
  Matrix matrix = buildDiagonalMatrix<Matrix>(nrow, VectorSize);

  // Launch our kernel
  typedef AddDiagonalValuesKernel<Matrix> kernel;
  kernel::apply(matrix);

  // Check the result
  success = kernel::check(matrix, out);
}

TEUCHOS_UNIT_TEST_TEMPLATE_3_DECL(
  Kokkos_CrsMatrix_MP, SumIntoValuesAtomic, Scalar, Ordinal, Device )
{
  const Ordinal VectorSize = 3;
  typedef Stokhos::StaticFixedStorage<Ordinal,Scalar,VectorSize,Device> Storage;
  typedef Sacado::MP::Vector<Storage> MatrixScalar;
  typedef Kokkos::CrsMatrix<MatrixScalar,Ordinal,Device> Matrix;

  // Build diagonal matrix
  Ordinal nrow = 10;
  Matrix matrix = buildDiagonalMatrix<Matrix>(nrow, VectorSize);

  // Launch our kernel
  typedef AddDiagonalValuesAtomicKernel<Matrix> kernel;
  kernel::apply(matrix);

  // Check the result
  success = kernel::check(matrix, out);
}

template <typename VectorType, typename Multiply>
bool test_embedded_vector(const typename VectorType::ordinal_type nGrid,
                          const typename VectorType::ordinal_type stoch_length,
                          Kokkos::DeviceConfig dev_config,
                          Multiply multiply_op,
                          Teuchos::FancyOStream& out)
{
  typedef typename VectorType::ordinal_type ordinal_type;
  typedef typename VectorType::value_type scalar_type;
  typedef typename VectorType::storage_type storage_type;
  typedef typename storage_type::device_type device_type;
  typedef Kokkos::LayoutRight Layout;
  typedef Kokkos::View< VectorType*, Layout, device_type > block_vector_type;
  typedef Kokkos::CrsMatrix< VectorType, ordinal_type, device_type > block_matrix_type;
  typedef typename block_matrix_type::StaticCrsGraphType matrix_graph_type;
  typedef typename block_matrix_type::values_type matrix_values_type;

  // Check ensemble_length == storage_type::static_size for static storage
  TEUCHOS_TEST_FOR_EXCEPTION(
    storage_type::is_static && storage_type::static_size != stoch_length,
    std::logic_error,
    "Static storage size must equal ensemble size");

  // Generate FEM graph:
  ordinal_type fem_length = nGrid * nGrid * nGrid;
  std::vector< std::vector<ordinal_type> > fem_graph;
  ordinal_type fem_graph_length = generate_fem_graph( nGrid, fem_graph );

  //------------------------------
  // Generate input multivector:

  block_vector_type x =
    block_vector_type(Kokkos::allocate_without_initializing,
                      "x", fem_length, stoch_length);
  block_vector_type y =
    block_vector_type(Kokkos::allocate_without_initializing,
                      "y", fem_length, stoch_length);

  typename block_vector_type::HostMirror hx = Kokkos::create_mirror_view( x );
  typename block_vector_type::HostMirror hy = Kokkos::create_mirror_view( y );

  // View the block vector as an array of the embedded intrinsic type.
  typename block_vector_type::HostMirror::array_type hax = hx ;
  typename block_vector_type::HostMirror::array_type hay = hy ;

  for (ordinal_type iRowFEM=0; iRowFEM<fem_length; ++iRowFEM) {
    for (ordinal_type iRowStoch=0; iRowStoch<stoch_length; ++iRowStoch) {
      hax(iRowFEM,iRowStoch) =
        generate_vector_coefficient<scalar_type>(
          fem_length, stoch_length, iRowFEM, iRowStoch );
      hay(iRowFEM,iRowStoch) = 0.0;
    }
  }

  Kokkos::deep_copy( x, hx );
  Kokkos::deep_copy( y, hy );

  //------------------------------
  // Generate block matrix

  matrix_graph_type matrix_graph =
    Kokkos::create_staticcrsgraph<matrix_graph_type>(
      std::string("test crs graph"), fem_graph);
  matrix_values_type matrix_values =
    matrix_values_type(
      Kokkos::allocate_without_initializing,
      "matrix", fem_graph_length, stoch_length);
  block_matrix_type matrix(
    "block_matrix", fem_length, matrix_values, matrix_graph);
  matrix.dev_config = dev_config;

  typename matrix_values_type::HostMirror hM =
    Kokkos::create_mirror_view( matrix.values );

  typename matrix_values_type::HostMirror::array_type haM = hM ;

  for (ordinal_type iRowFEM=0, iEntryFEM=0; iRowFEM<fem_length; ++iRowFEM) {
    const ordinal_type row_size = fem_graph[iRowFEM].size();
    for (ordinal_type iRowEntryFEM=0; iRowEntryFEM<row_size;
         ++iRowEntryFEM, ++iEntryFEM) {
      const ordinal_type iColFEM = fem_graph[iRowFEM][iRowEntryFEM];

      for (ordinal_type k=0; k<stoch_length; ++k) {
        haM(iEntryFEM,k) =
          generate_matrix_coefficient<scalar_type>(
            fem_length, stoch_length, iRowFEM, iColFEM, k);
      }
    }
  }

  Kokkos::deep_copy( matrix.values, hM );

  //------------------------------
  // multiply

  multiply_op( matrix, x, y );

  //------------------------------
  // generate correct answer

  typedef typename block_vector_type::array_type array_type;
  array_type ay_expected =
    array_type("ay_expected", fem_length, stoch_length);
  typename array_type::HostMirror hay_expected =
    Kokkos::create_mirror_view(ay_expected);
  for (ordinal_type iRowFEM=0, iEntryFEM=0; iRowFEM<fem_length; ++iRowFEM) {
    const ordinal_type row_size = fem_graph[iRowFEM].size();
    for (ordinal_type iRowEntryFEM=0; iRowEntryFEM<row_size;
         ++iRowEntryFEM, ++iEntryFEM) {
      const ordinal_type iColFEM = fem_graph[iRowFEM][iRowEntryFEM];
      for (ordinal_type k=0; k<stoch_length; ++k) {
        hay_expected(iRowFEM, k) +=
          generate_matrix_coefficient<scalar_type>(
            fem_length, stoch_length, iRowFEM, iColFEM, k) *
          generate_vector_coefficient<scalar_type>(
            fem_length, stoch_length, iColFEM, k );
      }
    }
  }
  Kokkos::deep_copy( ay_expected, hay_expected );

  //------------------------------
  // check

  typename block_vector_type::array_type ay = y;
  scalar_type rel_tol = ScalarTol<scalar_type>::tol();
  scalar_type abs_tol = ScalarTol<scalar_type>::tol();
  bool success = compare_rank_2_views(ay, ay_expected, rel_tol, abs_tol, out);

  return success;
}

struct Kokkos_MV_Multiply_Op {
  template <typename Matrix, typename InputVector, typename OutputVector>
  void operator() (const Matrix& A,
                   const InputVector& x,
                   OutputVector& y) const {
    Kokkos::MV_Multiply(y, A, x);
  }
};

template <typename Tag>
struct Stokhos_MV_Multiply_Op {
  Tag tag;
  Stokhos_MV_Multiply_Op(const Tag& tg = Tag()) : tag(tg) {}

  template <typename Matrix, typename InputVector, typename OutputVector>
  void operator() (const Matrix& A,
                   const InputVector& x,
                   OutputVector& y) const {
    Stokhos::multiply(A, x, y, tag);
  }
};

typedef Kokkos_MV_Multiply_Op KokkosMultiply;
typedef Stokhos_MV_Multiply_Op<Stokhos::EnsembleMultiply> EnsembleMultiply;
typedef Stokhos_MV_Multiply_Op<Stokhos::DefaultMultiply> DefaultMultiply;

#define CRSMATRIX_MP_VECTOR_TESTS_SCALAR_ORDINAL_DEVICE(SCALAR, ORDINAL, DEVICE)\
TEUCHOS_UNIT_TEST_TEMPLATE_3_INSTANT(                                   \
  Kokkos_CrsMatrix_MP, ReplaceValues, SCALAR, ORDINAL, DEVICE )         \
TEUCHOS_UNIT_TEST_TEMPLATE_3_INSTANT(                                   \
  Kokkos_CrsMatrix_MP, SumIntoValues, SCALAR, ORDINAL, DEVICE )         \
TEUCHOS_UNIT_TEST_TEMPLATE_3_INSTANT(                                   \
  Kokkos_CrsMatrix_MP, SumIntoValuesAtomic, SCALAR, ORDINAL, DEVICE )