#pragma once

#include <filesystem>

#include "datatypeUtil.h"

struct ClData
{
    cl::Device &device;
    cl::Context &context;
    cl::Program &program;
    cl::CommandQueue &queue;

    cl::Buffer tImgBuf;
    cl::Buffer sImgBuf;
    cl::Buffer maskBuf;
};

void init(Image &templateImg, Image &scienceImg, ImageMask &mask, ClData& clData);
void sss(const Image &templateImg, const Image &scienceImg, ImageMask &mask, std::vector<Stamp> &templateStamps, std::vector<Stamp> &sciStamps);
void cmv(const Image &templateImg, const Image &scienceImg, ImageMask &mask, std::vector<Stamp> &templateStamps, std::vector<Stamp> &sciStamps, const Kernel &convolutionKernel);
void cd(Image &templateImg, Image &scienceImg, ImageMask &mask, std::vector<Stamp> &templateStamps, std::vector<Stamp> &sciStamps);
void ksc(const Image &templateImg, const Image &scienceImg, ImageMask &mask, std::vector<Stamp> &templateStamps, Kernel &convolutionKernel);
void conv(const Image &templateImg, const Image &scienceImg, ImageMask &mask, Image &convImg, Kernel &convolutionKernel,
          const cl::Context &context, const cl::Program &program, cl::CommandQueue &queue);
void sub(const Image &convImg, const Image &scienceImg, const ImageMask &mask, Image &diffImg,
         const cl::Context &context, const cl::Program &program, cl::CommandQueue &queue);
void fin(const Image &convImg, const Image &diffImg);
