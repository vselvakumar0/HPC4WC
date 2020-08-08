#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <omp.h>

#ifdef CRAYPAT
#include "pat_api.h"
#endif
#include "utils.h"

namespace {

void updateHalo(Storage3D<realType>& inField) {
  const int xInterior = inField.xMax() - inField.xMin();
  const int yInterior = inField.yMax() - inField.yMin();

#pragma omp parallel
  {
  // bottom edge (without corners)
#pragma omp for collapse(2) nowait
  for(std::size_t k = 0; k < inField.zMax(); ++k) {
    for(std::size_t j = 0; j < inField.yMin(); ++j) {
      for(std::size_t i = inField.xMin(); i < inField.xMax(); ++i) {
        inField(i, j, k) = inField(i, j + yInterior, k);
      }
    }
  }

  // top edge (without corners)
#pragma omp for collapse(2) nowait
  for(std::size_t k = 0; k < inField.zMax(); ++k) {
    for(std::size_t j = inField.yMax(); j < inField.ySize(); ++j) {
      for(std::size_t i = inField.xMin(); i < inField.xMax(); ++i) {
        inField(i, j, k) = inField(i, j - yInterior, k);
      }
    }
  }

  // left edge (including corners)
#pragma omp for collapse(2) nowait
  for(std::size_t k = 0; k < inField.zMax(); ++k) {
    for(std::size_t j = inField.yMin(); j < inField.yMax(); ++j) {
      for(std::size_t i = 0; i < inField.xMin(); ++i) {
        inField(i, j, k) = inField(i + xInterior, j, k);
      }
    }
  }

  // right edge (including corners)
#pragma omp for collapse(2) nowait
  for(std::size_t k = 0; k < inField.zMax(); ++k) {
    for(std::size_t j = inField.yMin(); j < inField.yMax(); ++j) {
      for(std::size_t i = inField.xMax(); i < inField.xSize(); ++i) {
        inField(i, j, k) = inField(i - xInterior, j, k);
      }
    }
  }
  }
}

void apply_diffusion(Storage3D<realType>& inField, Storage3D<realType>& outField, realType alpha,
                     unsigned numIter, int x, int y, int z, int halo) {

  Storage3D<realType> tmp1Field(x, y, z, halo);

  for(std::size_t iter = 0; iter < numIter; ++iter) {

    updateHalo(inField);

    for(std::size_t k = 0; k < inField.zMax(); ++k) {

      // apply the initial laplacian
#pragma omp parallel for
      for(std::size_t j = inField.yMin() - 1; j < inField.yMax() + 1; ++j) {
        for(std::size_t i = inField.xMin() - 1; i < inField.xMax() + 1; ++i) {
          tmp1Field(i, j, 0) = -4.0 * inField(i, j, k) + inField(i - 1, j, k) +
                               inField(i + 1, j, k) + inField(i, j - 1, k) + inField(i, j + 1, k);
        }
      }

      // apply the second laplacian
#pragma omp parallel for
      for(std::size_t j = inField.yMin(); j < inField.yMax(); ++j) {
        for(std::size_t i = inField.xMin(); i < inField.xMax(); ++i) {
          realType laplap = -4.0 * tmp1Field(i, j, 0) + tmp1Field(i - 1, j, 0) +
                          tmp1Field(i + 1, j, 0) + tmp1Field(i, j - 1, 0) + tmp1Field(i, j + 1, 0);

          // and update the field
          if(iter == numIter - 1) {
            outField(i, j, k) = inField(i, j, k) - alpha * laplap;
          } else {
            inField(i, j, k) = inField(i, j, k) - alpha * laplap;
          }
        }
      }
    }
  }
}

void reportTime(const Storage3D<realType>& storage, int nIter, double diff) {
  std::cout << "# ranks nx ny ny nz num_iter time\ndata = np.array( [ \\\n";
  int size;
#pragma omp parallel
  {
#pragma omp master
    { size = omp_get_num_threads(); }
  }
  std::cout << "[ " << size << ", " << storage.xMax() - storage.xMin() << ", "
            << storage.yMax() - storage.yMin() << ", " << storage.zMax() << ", " << nIter << ", "
            << diff << "],\n";
  std::cout << "] )" << std::endl;
}
} // namespace

int main(int argc, char const* argv[]) {
#ifdef CRAYPAT
  PAT_record(PAT_STATE_OFF);
#endif
  int x = atoi(argv[2]);
  int y = atoi(argv[4]);
  int z = atoi(argv[6]);
  int iter = atoi(argv[8]);
  int nHalo = 2;
  assert(x > 0 && y > 0 && z > 0 && iter > 0);
  Storage3D<realType> input(x, y, z, nHalo);
  input.initialize();
  Storage3D<realType> output(x, y, z, nHalo);
  output.initialize();

  realType alpha = 1. / 32.;

  std::ofstream fout;
  fout.open("in_field.dat", std::ios::binary | std::ofstream::trunc);
  input.writeFile(fout);
  fout.close();
#ifdef CRAYPAT
  PAT_record(PAT_STATE_ON);
#endif
  auto start = std::chrono::steady_clock::now();

  apply_diffusion(input, output, alpha, iter, x, y, z, nHalo);

  auto end = std::chrono::steady_clock::now();
#ifdef CRAYPAT
  PAT_record(PAT_STATE_OFF);
#endif
  updateHalo(output);
  fout.open("out_field.dat", std::ios::binary | std::ofstream::trunc);
  output.writeFile(fout);
  fout.close();

  auto diff = end - start;
  double timeDiff = std::chrono::duration<double, std::milli>(diff).count() / 1000.;
  reportTime(output, iter, timeDiff);

  return 0;
}
