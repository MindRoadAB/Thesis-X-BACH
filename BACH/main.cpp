#define CL_HPP_TARGET_OPENCL_VERSION 300

#include <CL/opencl.hpp>
#include <iostream>
#include <vector>

#include "utils/argsUtil.h"
#include "utils/bachUtil.h"
#include "utils/clUtil.h"
#include "utils/fitsUtil.h"

int main(int argc, char* argv[]) {
  CCfits::FITS::setVerboseMode(true);

  try {
    getArguments(argc, argv);
  } catch(const std::invalid_argument& err) {
    std::cout << err.what() << '\n';
    return 1;
  }

  Image templateImg{args.templateName};
  Image scienceImg{args.scienceName};
  cl_int err{};

  err = readImage(templateImg);
  checkError(err);

  cl::Device default_device{get_default_device()};
  cl::Context context{default_device};

  cl::Program program =
      load_build_programs(context, default_device, "conv.cl", "sub.cl");

  auto [w, h] = templateImg.axis;

  cl::Buffer imgbuf(context, CL_MEM_READ_WRITE, sizeof(cl_double) * w * h);
  cl::Buffer outimgbuf(context, CL_MEM_READ_WRITE, sizeof(cl_double) * w * h);
  cl::Buffer diffimgbuf(context, CL_MEM_READ_WRITE, sizeof(cl_double) * w * h);

  // box 5x5
  cl_long kernWidth = 5;
  cl_double a = 1.0 / (cl_double)(kernWidth * kernWidth);
  cl_double convKern[] = {a, a, a, a, a, a, a, a, a, a, a, a, a,
                          a, a, a, a, a, a, a, a, a, a, a, a};

  cl::Buffer kernbuf(context, CL_MEM_READ_WRITE,
                     sizeof(cl_double) * kernWidth * kernWidth);

  cl::CommandQueue queue(context, default_device);

  err = queue.enqueueWriteBuffer(
      kernbuf, CL_TRUE, 0, sizeof(cl_double) * kernWidth * kernWidth, convKern);
  checkError(err);
  err = queue.enqueueWriteBuffer(imgbuf, CL_TRUE, 0, sizeof(cl_double) * w * h,
                                 templateImg.data);
  checkError(err);

  cl::KernelFunctor<cl::Buffer, cl_long, cl::Buffer, cl::Buffer, cl_long,
                    cl_long>
      conv{program, "conv"};
  cl::EnqueueArgs eargs{queue, cl::NullRange, cl::NDRange(w * h),
                        cl::NullRange};
  conv(eargs, kernbuf, kernWidth, imgbuf, outimgbuf, w, h);

  Image outImg{args.outName, templateImg.axis, args.outPath};
  err = queue.enqueueReadBuffer(outimgbuf, CL_TRUE, 0,
                                sizeof(cl_double) * w * h, outImg.data);
  checkError(err);

  err = writeImage(outImg);
  checkError(err);

  cl::KernelFunctor<cl::Buffer, cl::Buffer, cl::Buffer> sub{program, "sub"};
  sub(eargs, outimgbuf, imgbuf, diffimgbuf);

  Image diffImg{"sub.fits", templateImg.axis, args.outPath};
  err = queue.enqueueReadBuffer(diffimgbuf, CL_TRUE, 0,
                                sizeof(cl_double) * w * h, diffImg.data);
  checkError(err);

  err = writeImage(diffImg);
  checkError(err);

  return 0;
}
