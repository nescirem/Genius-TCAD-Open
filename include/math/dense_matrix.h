// $Id: dense_matrix.h 2789 2008-04-13 02:24:40Z roystgnr $

// The libMesh Finite Element Library.
// Copyright (C) 2002-2007  Benjamin S. Kirk, John W. Peterson

// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA



#ifndef __dense_matrix_h__
#define __dense_matrix_h__

// C++ includes
#include <vector>
#include <algorithm>

// Local Includes
#include "genius_common.h"
#include "dense_matrix_base.h"

// Forward Declarations
template <typename T> class DenseVector;



/**
 * Defines a dense matrix for use in Finite Element-type computations.
 * Useful for storing element stiffness matrices before summation
 * into a global matrix.
 *
 * @author Benjamin S. Kirk, 2002
 */

// ------------------------------------------------------------
// Dense Matrix class definition
template<typename T>
class DenseMatrix : public DenseMatrixBase<T>
{
public:

  /**
   * Constructor.  Creates a dense matrix of dimension \p m by \p n.
   */
  DenseMatrix(const unsigned int m=0,
          const unsigned int n=0);

  /**
   * Copy-constructor.
   */
  DenseMatrix (const DenseMatrix<T>& other_matrix);

  /**
   * Destructor.  Empty.
   */
  virtual ~DenseMatrix() {}


  /**
   * Set every element in the matrix to 0.
   */
  virtual void zero();

  /**
   * @returns the \p (i,j) element of the matrix.
   */
  T operator() (const unsigned int i,
        const unsigned int j) const;

  /**
   * @returns the \p (i,j) element of the matrix as a writeable reference.
   */
  T & operator() (const unsigned int i,
          const unsigned int j);

  /**
   * @returns the \p (i,j) element of the matrix as a writeable reference.
   */
  virtual T el(const unsigned int i,
           const unsigned int j) const { return (*this)(i,j); }

  /**
   * @returns the \p (i,j) element of the matrix as a writeable reference.
   */
  virtual T & el(const unsigned int i,
         const unsigned int j)     { return (*this)(i,j); }

  /**
   * Left multipliess by the matrix \p M2.
   */
  virtual void left_multiply (const DenseMatrixBase<T>& M2);

  /**
   * Right multiplies by the matrix \p M3.
   */
  virtual void right_multiply (const DenseMatrixBase<T>& M3);

  /**
   * Assignment operator.
   */
  DenseMatrix<T>& operator = (const DenseMatrix<T>& other_matrix);

  /**
   * STL-like swap method
   */
  void swap(DenseMatrix<T>& other_matrix);

  /**
   * Resize the matrix.  Will never free memory, but may
   * allocate more.  Sets all elements to 0.
   */
  void resize(const unsigned int m,
          const unsigned int n);

  /**
   * Multiplies every element in the matrix by \p factor.
   */
  void scale (const T factor);

  /**
   * Multiplies every element in the matrix by \p factor.
   */
  DenseMatrix<T>& operator *= (const T factor);

  /**
   * Adds \p factor times \p mat to this matrix.
   */
  void add (const T factor,
            const DenseMatrix<T>& mat);

  /**
   * Adds \p mat to this matrix.
   */
  DenseMatrix<T>& operator+= (const DenseMatrix<T> &mat);

  /**
   * @returns the minimum element in the matrix.
   * In case of complex numbers, this returns the minimum
   * Real part.
   */
  Real min () const;

  /**
   * @returns the maximum element in the matrix.
   * In case of complex numbers, this returns the maximum
   * Real part.
   */
  Real max () const;

  /**
   * Return the l1-norm of the matrix, that is
   * \f$|M|_1=max_{all columns j}\sum_{all
   * rows i} |M_ij|\f$,
   * (max. sum of columns).
   * This is the
   * natural matrix norm that is compatible
   * to the l1-norm for vectors, i.e.
   * \f$|Mv|_1\leq |M|_1 |v|_1\f$.
   */
  Real l1_norm () const;

  /**
   * Return the linfty-norm of the
   * matrix, that is
   * \f$|M|_\infty=max_{all rows i}\sum_{all
   * columns j} |M_ij|\f$,
   * (max. sum of rows).
   * This is the
   * natural matrix norm that is compatible
   * to the linfty-norm of vectors, i.e.
   * \f$|Mv|_\infty \leq |M|_\infty |v|_\infty\f$.
   */
  Real linfty_norm () const;

  /**
   * Left multiplies by the transpose of the matrix \p A.
   */
  void left_multiply_transpose (const DenseMatrix<T>& A);


  /**
   * Right multiplies by the transpose of the matrix \p A
   */
  void right_multiply_transpose (const DenseMatrix<T>& A);

  /**
   * @returns the \p (i,j) element of the transposed matrix.
   */
  T transpose (const unsigned int i,
           const unsigned int j) const;

  /**
   * Access to the values array.  This should be used with
   * caution but can  be used to speed up code compilation
   * significantly.
   */
  std::vector<T>& get_values() { return _val; }

  /**
   * Return a constant reference to the matrix values.
   */
  const std::vector<T>& get_values() const { return _val; }

  /**
   * Condense-out the \p (i,j) entry of the matrix, forcing
   * it to take on the value \p val.  This is useful in numerical
   * simulations for applying boundary conditions.  Preserves the
   * symmetry of the matrix.
   */
  void condense(const unsigned int i,
        const unsigned int j,
        const T val,
        DenseVector<T>& rhs)
  { DenseMatrixBase<T>::condense (i, j, val, rhs); }


  /**
   * Solve the system Ax=b given the input vector b.
   */
  void lu_solve (DenseVector<T>& b,
         DenseVector<T>& x,
         const bool partial_pivot = false);



  /**
   * For symmetric positive definite (SPD) matrices. A Cholesky factorization
   * of A such that A = L L^T is about twice as fast as a standard LU
   * factorization.  Therefore you can use this method if you know a-priori
   * that the matrix is SPD.  If the matrix is not SPD, an error is generated.
   * One nice property of cholesky decompositions is that they do not require
   * pivoting for stability. Note that this method may also be used when
   * A is real-valued and x and b are complex-valued.
   */
  template <typename T2>
  void cholesky_solve(DenseVector<T2>& b,
              DenseVector<T2>& x);


  /**
   * @returns the determinant of the matrix.  Note that this means
   * doing an LU decomposition and then computing the product of the
   * diagonal terms.  Therefore this is a non-const method.
   */
  T det();

  /**
   * Computes the inverse of the dense matrix (assuming it is invertible)
   * by first computing the LU decomposition and then performing multiple
   * back substitution steps.  Follows the algorithm from Numerical Recipes
   * in C available on the web.  This is not the most memory efficient routine since
   * the inverse is not computed "in place" but is instead placed into a the
   * matrix inv passed in by the user.
   */
  // void inverse();

private:

  /**
   * The actual data values, stored as a 1D array.
   */
  std::vector<T> _val;

  /**
   * Form the LU decomposition of the matrix.  This function
   * is private since it is only called as part of the implementation
   * of the lu_solve(...) function.
   */
  void _lu_decompose (const bool partial_pivot = false);

  /**
   * Solves the system Ax=b through back substitution.  This function
   * is private since it is only called as part of the implementation
   * of the lu_solve(...) function.
   */
  void _lu_back_substitute (DenseVector<T>& b,
                DenseVector<T>& x,
                const bool partial_pivot = false) const;

  /**
   * Decomposes a symmetric positive definite matrix into a
   * product of two lower triangular matrices according to
   * A = LL^T.  Note that this program generates an error
   * if the matrix is not SPD.
   */
  void _cholesky_decompose();

  /**
   * Solves the equation Ax=b for the unknown value x and rhs
   * b based on the Cholesky factorization of A. Note that
   * this method may be used when A is real-valued and b and x
   * are complex-valued.
   */
  template <typename T2>
  void _cholesky_back_substitute(DenseVector<T2>& b,
                 DenseVector<T2>& x) const;

  /**
   * The decomposition schemes above change the entries of the matrix
   * A.  It is therefore an error to call A.lu_solve() and subsequently
   * call A.cholesky_solve() since the result will probably not match
   * any desired outcome.  This typedef keeps track of which decomposition
   * has been called for this matrix.
   */
  enum DecompositionType {LU=0, CHOLESKY=1, NONE};

  /**
   * This flag keeps track of which type of decomposition has been
   * performed on the matrix.
   */
  DecompositionType _decomposition_type;
};





// ------------------------------------------------------------
/**
 * Provide Typedefs for dense matrices
 */
namespace DenseMatrices
{

  /**
   * Convenient definition of a real-only
   * dense matrix.
   */
  typedef DenseMatrix<Real> RealDenseMatrix;

  /**
   * Note that this typedef may be either
   * a real-only matrix, or a truly complex
   * matrix, depending on how \p Number
   * was defined in \p libmesh_common.h.
   * Be also aware of the fact that \p DenseMatrix<T>
   * is likely to be more efficient for
   * real than for complex data.
   */
  typedef DenseMatrix<Complex> ComplexDenseMatrix;

}



using namespace DenseMatrices;





// ------------------------------------------------------------
// Dense Matrix member functions
template<typename T>
inline
DenseMatrix<T>::DenseMatrix(const unsigned int m,
                const unsigned int n)
  : DenseMatrixBase<T>(m,n),
    _decomposition_type(NONE)
{
  this->resize(m,n);
}



template<typename T>
inline
DenseMatrix<T>::DenseMatrix (const DenseMatrix<T>& other_matrix)
  : DenseMatrixBase<T>(other_matrix._m, other_matrix._n)
{
  _val = other_matrix._val;
}



template<typename T>
inline
void DenseMatrix<T>::swap(DenseMatrix<T>& other_matrix)
{
  std::swap(this->_m, other_matrix._m);
  std::swap(this->_n, other_matrix._n);
  _val.swap(other_matrix._val);
  DecompositionType _temp = _decomposition_type;
  _decomposition_type = other_matrix._decomposition_type;
  other_matrix._decomposition_type = _temp;
}



template<typename T>
inline
void DenseMatrix<T>::resize(const unsigned int m,
                const unsigned int n)
{
  _val.resize(m*n);

  this->_m = m;
  this->_n = n;

  _decomposition_type = NONE;
  this->zero();
}



template<typename T>
inline
void DenseMatrix<T>::zero()
{
  _decomposition_type = NONE;

  std::fill (_val.begin(), _val.end(), 0.);
}



template<typename T>
inline
DenseMatrix<T>& DenseMatrix<T>::operator = (const DenseMatrix<T>& other_matrix)
{
  this->_m = other_matrix._m;
  this->_n = other_matrix._n;

  _val                = other_matrix._val;
  _decomposition_type = other_matrix._decomposition_type;

  return *this;
}



template<typename T>
inline
T DenseMatrix<T>::operator () (const unsigned int i,
                   const unsigned int j) const
{
  assert (i*j<_val.size());
  assert (i < this->_m);
  assert (j < this->_n);


  //  return _val[(i) + (this->_m)*(j)]; // col-major
  return _val[(i)*(this->_n) + (j)]; // row-major
}



template<typename T>
inline
T & DenseMatrix<T>::operator () (const unsigned int i,
                 const unsigned int j)
{
  assert (i*j<_val.size());
  assert (i < this->_m);
  assert (j < this->_n);

  //return _val[(i) + (this->_m)*(j)]; // col-major
  return _val[(i)*(this->_n) + (j)]; // row-major
}





template<typename T>
inline
void DenseMatrix<T>::scale (const T factor)
{
  for (unsigned int i=0; i<_val.size(); i++)
    _val[i] *= factor;
}



template<typename T>
inline
DenseMatrix<T>& DenseMatrix<T>::operator *= (const T factor)
{
  this->scale(factor);
  return *this;
}



template<typename T>
inline
void DenseMatrix<T>::add (const T factor, const DenseMatrix<T>& mat)
{
  for (unsigned int i=0; i<_val.size(); i++)
    _val[i] += factor * mat._val[i];
}



template<typename T>
inline
DenseMatrix<T>& DenseMatrix<T>::operator += (const DenseMatrix<T> &mat)
{
  for (unsigned int i=0; i<_val.size(); i++)
    _val[i] += mat._val[i];

  return *this;
}



template<typename T>
inline
Real DenseMatrix<T>::min () const
{
  assert (this->_m);
  assert (this->_n);
  Real my_min = genius_real((*this)(0,0));

  for (unsigned int i=0; i!=this->_m; i++)
    {
      for (unsigned int j=0; j!=this->_n; j++)
        {
          Real current = genius_real((*this)(i,j));
          my_min = (my_min < current? my_min : current);
        }
    }
  return my_min;
}



template<typename T>
inline
Real DenseMatrix<T>::max () const
{
  assert (this->_m);
  assert (this->_n);
  Real my_max = genius_real((*this)(0,0));

  for (unsigned int i=0; i!=this->_m; i++)
    {
      for (unsigned int j=0; j!=this->_n; j++)
        {
          Real current = genius_real((*this)(i,j));
          my_max = (my_max > current? my_max : current);
        }
    }
  return my_max;
}



template<typename T>
inline
Real DenseMatrix<T>::l1_norm () const
{
  assert (this->_m);
  assert (this->_n);

  Real columnsum = 0.;
  for (unsigned int i=0; i!=this->_m; i++)
    {
      columnsum += std::abs((*this)(i,0));
    }
  Real my_max = columnsum;
  for (unsigned int j=1; j!=this->_n; j++)
    {
      columnsum = 0.;
      for (unsigned int i=0; i!=this->_m; i++)
        {
          columnsum += std::abs((*this)(i,j));
        }
      my_max = (my_max > columnsum? my_max : columnsum);
    }
  return my_max;
}



template<typename T>
inline
Real DenseMatrix<T>::linfty_norm () const
{
  assert (this->_m);
  assert (this->_n);

  Real rowsum = 0.;
  for (unsigned int j=0; j!=this->_n; j++)
    {
      rowsum += std::abs((*this)(0,j));
    }
  Real my_max = rowsum;
  for (unsigned int i=1; i!=this->_m; i++)
    {
      rowsum = 0.;
      for (unsigned int j=0; j!=this->_n; j++)
        {
          rowsum += std::abs((*this)(i,j));
        }
      my_max = (my_max > rowsum? my_max : rowsum);
    }
  return my_max;
}



template<typename T>
inline
T DenseMatrix<T>::transpose (const unsigned int i,
                 const unsigned int j) const
{
  // Implement in terms of operator()
  return (*this)(j,i);
}





// template<typename T>
// inline
// void DenseMatrix<T>::condense(const unsigned int iv,
//                const unsigned int jv,
//                const T val,
//                DenseVector<T>& rhs)
// {
//   assert (this->_m == rhs.size());
//   assert (iv == jv);


//   // move the known value into the RHS
//   // and zero the column
//   for (unsigned int i=0; i<this->m(); i++)
//     {
//       rhs(i) -= ((*this)(i,jv))*val;
//       (*this)(i,jv) = 0.;
//     }

//   // zero the row
//   for (unsigned int j=0; j<this->n(); j++)
//     (*this)(iv,j) = 0.;

//   (*this)(iv,jv) = 1.;
//   rhs(iv) = val;

// }







#endif // #ifndef __dense_matrix_h__

