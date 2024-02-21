#include "bachUtil.h"
#include <cassert>

void identifySStamps(std::vector<Stamp>& templStamps, const Image& templImage, std::vector<Stamp>& scienceStamps, const Image& scienceImage, ImageMask& mask, double* filledTempl, double* filledScience, const Arguments& args) {
  std::cout << "Identifying sub-stamps in " << templImage.name << " and " << scienceImage.name << "..." << std::endl;

  assert(templStamps.size() == scienceStamps.size());

  for (int i = 0; i < templStamps.size(); i++) {
    calcStats(templStamps[i], templImage, mask, args);
    calcStats(scienceStamps[i], scienceImage, mask, args);

    findSStamps(templStamps[i], templImage, mask, i, true, args);
    findSStamps(scienceStamps[i], scienceImage, mask, i, false, args);
  }

  int oldCount = templStamps.size();

  templStamps.erase(std::remove_if(templStamps.begin(), templStamps.end(),
                              [](Stamp& s) { return s.subStamps.empty(); }),
               templStamps.end());
  scienceStamps.erase(std::remove_if(scienceStamps.begin(), scienceStamps.end(),
                              [](Stamp& s) { return s.subStamps.empty(); }),
               scienceStamps.end());

  if (filledTempl != nullptr) {
    *filledTempl = static_cast<double>(templStamps.size()) / oldCount;
  }

  if (filledScience != nullptr) {
    *filledScience = static_cast<double>(scienceStamps.size()) / oldCount;
  }

  if(args.verbose) {
    std::cout << "Non-Empty template stamps: " << templStamps.size() << std::endl;
    std::cout << "Non-Empty science stamps: " << scienceStamps.size() << std::endl;
  }
}

void createStamps(const Image& templateImg, const Image& scienceImg, std::vector<Stamp>& templateStamps, std::vector<Stamp>& scienceStamps, const int w, const int h, const Arguments& args, const ClData& clData) {
  cl::EnqueueArgs eargsBounds{clData.queue, cl::NullRange, cl::NDRange(args.stampsx * args.stampsy), cl::NullRange};

  cl::KernelFunctor<cl::Buffer, cl::Buffer, cl_int, cl_int, cl_long, cl_long, cl_long>
  boundsFunc(clData.program, "createStampBounds");

  cl::Event boundsEvent = 
      boundsFunc(eargsBounds,
                  clData.stampsCoordsBuf, clData.stampsSizesBuf,
                  args.stampsx, args.stampsy, args.fStampWidth,
                  w, h);
  boundsEvent.wait();

  std::vector<cl_long> stampCoords(2 * args.stampsx * args.stampsy, 0);
  std::vector<cl_long> stampSizes(2 * args.stampsx * args.stampsy, 0);

  cl_int err = clData.queue.enqueueReadBuffer(clData.stampsCoordsBuf, CL_TRUE, 0,
    sizeof(cl_long) * 2 * args.stampsx * args.stampsy, &stampCoords[0]);
  checkError(err);
  err = clData.queue.enqueueReadBuffer(clData.stampsSizesBuf, CL_TRUE, 0,
    sizeof(cl_long) * 2 * args.stampsx * args.stampsy, &stampSizes[0]);
  checkError(err);

  for(int j = 0; j < args.stampsy; j++) {
    for(int i = 0; i < args.stampsx; i++) {
      int startx = stampCoords[2*(j*args.stampsx + i) + 0];
      int starty = stampCoords[2*(j*args.stampsx + i) + 1];
      int stampw =  stampSizes[2*(j*args.stampsx + i) + 0];
      int stamph =  stampSizes[2*(j*args.stampsx + i) + 1];

      printf("%d, %3d, %3d, %3d, %d, %d\n", i, j, startx, starty, stampw, stamph);
      templateStamps.emplace_back();
      scienceStamps.emplace_back();
      
      templateStamps.back().data.reserve(args.fStampWidth*args.fStampWidth);
      templateStamps.back().coords = std::make_pair(startx, starty);
      templateStamps.back().size = std::make_pair(stampw, stamph);
      
      scienceStamps.back().data.reserve(args.fStampWidth*args.fStampWidth);
      scienceStamps.back().coords = std::make_pair(startx, starty);
      scienceStamps.back().size = std::make_pair(stampw, stamph);
      
      for(int y = 0; y < stamph; y++) {
        for(int x = 0; x < stampw; x++) {
          templateStamps.back().data.push_back(
            templateImg[(startx + x) + ((starty + y) * w)]);
          scienceStamps.back().data.push_back(
            scienceImg[(startx + x) + ((starty + y) * w)]);
        }
      }
    }
  }


}

double checkSStamp(const SubStamp& sstamp, const Image& image, ImageMask& mask, const Stamp& stamp, const ImageMask::masks badMask, const bool isTemplate, const Arguments& args) {
  double retVal = 0.0;
  for(int y = sstamp.imageCoords.second - args.hSStampWidth;
      y <= sstamp.imageCoords.second + args.hSStampWidth; y++) {
    if(y < stamp.coords.second || y >= stamp.coords.second + stamp.size.second)
      continue;
    for(int x = sstamp.imageCoords.first - args.hSStampWidth;
        x <= sstamp.imageCoords.first + args.hSStampWidth; x++) {
      if(x < stamp.coords.first || x >= stamp.coords.first + stamp.size.first)
        continue;

      int absCoords = x + y * image.axis.first;
      if(mask.isMasked(absCoords, badMask))
        return 0.0;

      if(image[absCoords] >= args.threshHigh) {
        mask.maskPix(x, y, isTemplate ? ImageMask::BAD_PIXEL_T : ImageMask::BAD_PIXEL_S);
        return 0.0;
      }
      if((image[absCoords] - stamp.stats.skyEst) / stamp.stats.fwhm >
         args.threshKernFit)
        retVal += image[absCoords];
    }
  }
  return retVal;
}

cl_int findSStamps(Stamp& stamp, const Image& image, ImageMask& mask, const int index, const bool isTemplate, const Arguments& args) {
  double floor = stamp.stats.skyEst + args.threshKernFit * stamp.stats.fwhm;

  double dfrac = 0.9;
  int maxSStamps = 2 * args.maxKSStamps;

  ImageMask::masks badMask = ImageMask::ALL & ~ImageMask::OK_CONV;

  if (isTemplate) {
    badMask &= ~(ImageMask::BAD_PIXEL_S | ImageMask::SKIP_S);
  }
  else {
    badMask &= ~(ImageMask::BAD_PIXEL_T | ImageMask::SKIP_T);
  }

  while(stamp.subStamps.size() < size_t(maxSStamps)) {
    double lowestPSFLim =
        std::max(floor, stamp.stats.skyEst +
                            (args.threshHigh - stamp.stats.skyEst) * dfrac);
    for(long y = 0; y < args.fStampWidth; y++) {
      long absy = y + stamp.coords.second;
      for(long x = 0; x < args.fStampWidth; x++) {
        long absx = x + stamp.coords.first;
        long coords = x + (y * stamp.size.first);
        long absCoords = absx + (absy * image.axis.first);

        if (mask.isMasked(absCoords, badMask)) {
          continue;
        }

        if(stamp[coords] > args.threshHigh) {
          mask.maskPix(absx, absy, isTemplate ? ImageMask::BAD_PIXEL_T : ImageMask::BAD_PIXEL_S);
          continue;
        }

        if((stamp[coords] - stamp.stats.skyEst) * (1.0 / stamp.stats.fwhm) <
           args.threshKernFit) {
          continue;
        }

        if(stamp[coords] > lowestPSFLim) {  // good candidate found
          SubStamp s{{},
                     0.0,
                     std::make_pair(absx, absy),
                     std::make_pair(x, y),
                     stamp[coords]};
          
          for(long ky = absy - args.hSStampWidth;
              ky <= absy + args.hSStampWidth; ky++) {
            if(ky < stamp.coords.second ||
               ky >= stamp.coords.second + args.fStampWidth)
              continue;
            for(long kx = absx - args.hSStampWidth;
                kx <= absx + args.hSStampWidth; kx++) {
              if(kx < stamp.coords.first ||
                 kx >= stamp.coords.first + args.fStampWidth)
                continue;
              long kCoords = kx + (ky * image.axis.first);

              if (mask.isMasked(kCoords, badMask)) {
                continue;
              }

              if(image[kCoords] >= args.threshHigh) {
                mask.maskPix(kx, ky, isTemplate ? ImageMask::BAD_PIXEL_T : ImageMask::BAD_PIXEL_S);
                continue;
              }

              if((image[kCoords] - stamp.stats.skyEst) *
                     (1.0 / stamp.stats.fwhm) <
                 args.threshKernFit) {
                continue;
              }

              if(image[kCoords] > s.val) {
                s.val = image[kCoords];
                s.imageCoords = std::make_pair(kx, ky);
                s.stampCoords = std::make_pair(kx - stamp.coords.first,
                                               ky - stamp.coords.second);
              }
            }
          }
          s.val = checkSStamp(s, image, mask, stamp, badMask, isTemplate, args);
          if(s.val == 0.0) continue;
          stamp.subStamps.push_back(s);

          for(int y = s.stampCoords.second - args.hSStampWidth;
              y <= s.stampCoords.second + args.hSStampWidth; y++) {
            int y2 = y + stamp.coords.second;
            for(int x = s.stampCoords.first - args.hSStampWidth;
                x <= s.stampCoords.first + args.hSStampWidth; x++) {
              int x2 = x + stamp.coords.first;
              if (x > 0 && x < stamp.size.first && y > 0 && y < stamp.size.second) {
                mask.maskPix(x2, y2, isTemplate ? ImageMask::SKIP_T : ImageMask::SKIP_S);
              }
            }
          }
        }
        if(stamp.subStamps.size() >= size_t(maxSStamps)) break;
      }
      if(stamp.subStamps.size() >= size_t(maxSStamps)) break;
    }
    if(lowestPSFLim == floor) break;
    dfrac -= 0.2;
  }

  if(stamp.subStamps.size() == 0) {
    if(args.verbose)
      std::cout << "No suitable substamps found in stamp " << index
                << std::endl;
    return 1;
  }
  int keepSStampCount = std::min<int>(stamp.subStamps.size(), args.maxKSStamps);
  std::partial_sort(
    stamp.subStamps.begin(),
    stamp.subStamps.begin() + keepSStampCount,
    stamp.subStamps.end(),
    std::greater<SubStamp>()
    );

  if (stamp.subStamps.size() > keepSStampCount) {
    stamp.subStamps.erase(stamp.subStamps.begin() + keepSStampCount, stamp.subStamps.end());
  }

  if(args.verbose)
    std::cout << "Added " << stamp.subStamps.size() << " substamps to stamp "
              << index << std::endl;
  return 0;
}
