#ifndef ARG_UTIL
#define ARG_UTIL

#include <CL/opencl.hpp>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct Arguments {
  std::string templateName;
  std::string scienceName;
  std::string outName = "diff.fits";

  std::string inputPath = "res/";
  std::string outPath = "out/";

  int stampsx = 10;
  int stampsy = 10;

  cl_double threshLow = 0.0;
  cl_double threshHigh = 25000.0;
  cl_double threshKernFit = 20.0;

  cl_double sigClipAlpha = 3.0;
  cl_double iqRange = 1.35;  // interquartile range

  cl_int maxSStamps = 6;

  cl_int tmp_num_kernel_components = 3;  // TODO: set properly

  std::vector<cl_int> dg = {6, 4, 2};    // ngauss = length of dg
  cl_int hSStampWidth = 15;              // half substamp width
  cl_int fSStampWidth = 30;              // full substamp width
  cl_int hKernelWidth = 10;              // half kernel width
  cl_int fKernelWidth = 20;              // full kernel width

  std::vector<cl_int> dg = {6, 4, 2};
  std::vector<cl_double> bg = {
      (1.0 / (2.0 * 0.7 * 0.7)),
      (1.0 / (2.0 * 1.5 * 1.5)),
      (1.0 / (2.0 * 3.0 * 3.0)),
  };

  bool verbose = false;
};

inline char* getCmdOption(char** begin, char** end, const std::string& option) {
  char** itr = std::find(begin, end, option);
  if(itr != end && ++itr != end) {
    return *itr;
  }
  return 0;
}

inline bool cmdOptionExists(char** begin, char** end,
                            const std::string& option) {
  return std::find(begin, end, option) != end;
}

inline Arguments args{};

inline void getArguments(int argc, char* argv[]) {
  if(cmdOptionExists(argv, argv + argc, "-o")) {
    args.outName = getCmdOption(argv, argv + argc, "-o");
  }

  if(cmdOptionExists(argv, argv + argc, "-op")) {
    args.outPath = getCmdOption(argv, argv + argc, "-op");
  }

  if(cmdOptionExists(argv, argv + argc, "-ip")) {
    args.inputPath = getCmdOption(argv, argv + argc, "-ip");
  }

  if(cmdOptionExists(argv, argv + argc, "-v")) {
    args.verbose = true;
  }

  if(cmdOptionExists(argv, argv + argc, "-t")) {
    args.templateName = getCmdOption(argv, argv + argc, "-t");
  } else {
    throw std::invalid_argument("Template file Input is required!");
    return;
  }

  if(cmdOptionExists(argv, argv + argc, "-s")) {
    args.scienceName = getCmdOption(argv, argv + argc, "-s");
  } else {
    throw std::invalid_argument("Science file input is required!");
    return;
  }
}

#endif
