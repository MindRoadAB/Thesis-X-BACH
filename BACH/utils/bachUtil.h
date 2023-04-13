#ifndef BACH_UTIL
#define BACH_UTIL

#include <CL/opencl.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <utility>

#include "argsUtil.h"
#include "datatypeUtil.h"

inline void checkError(cl_int err) {
  if(err != 0) {
    std::cout << "Error encountered with error code: " << err << std::endl;
    exit(err);
  }
}

inline void maskInput(Image& img) {
  for(long y = 0; y < img.axis.second; y++) {
    for(long x = 0; x < img.axis.first; x++) {
      long index = x + y * img.axis.first;
      int borderSize = args.hSStampWidth + args.hKernelWidth;
      if(x < borderSize || x > img.axis.first - borderSize || y < borderSize ||
         y > img.axis.second - borderSize)
        img.maskPix(x, y, Image::edge);
      if(img[index] >= args.threshHigh || img[index] <= args.threshLow) {
        img.maskAroundPix(x, y, Image::badInput);
      }
      if(std::isnan(img[index])) {
        img.maskPix(x, y, Image::nan);
      }
    }
  }
}

inline void createStamps(Image& img, std::vector<Stamp>& stamps, int w, int h) {
  for(int j = 0; j < args.stampsy; j++) {
    for(int i = 0; i < args.stampsx; i++) {
      int stampw = w / args.stampsx;
      int stamph = h / args.stampsy;
      int startx = i * stampw;
      int starty = j * stamph;
      int stopx = startx + stampw;
      int stopy = starty + stamph;

      if(i == args.stampsx - 1) {
        stopx = w;
        stampw = stopx - startx;
      }

      if(j == args.stampsy - 1) {
        stopy = h;
        stamph = stopy - starty;
      }

      Stamp tmpS{};
      for(int y = 0; y < stamph; y++) {
        for(int x = 0; x < stampw; x++) {
          cl_double tmp = img[(startx + x) + ((starty + y) * w)];
          tmpS.data.push_back(tmp);
        }
      }

      tmpS.coords = std::make_pair(startx, starty);
      tmpS.size = std::make_pair(stampw, stamph);
      stamps.push_back(tmpS);
    }
  }
}

inline bool inImage(Image& image, int x, int y) {
  return !(x < 0 || x > image.axis.first || y < 0 || y > image.axis.second);
}

inline double checkSStamp(SubStamp& sstamp, Image& image, Stamp& stamp) {
  double retVal = 0.0;
  for(int y = sstamp.imageCoords.second - args.hSStampWidth;
      y < sstamp.imageCoords.second + args.hSStampWidth; y++) {
    if(y < 0 || y >= image.axis.second) continue;
    for(int x = sstamp.imageCoords.first - args.hSStampWidth;
        x < sstamp.imageCoords.first + args.hSStampWidth; x++) {
      if(x < 0 || x >= image.axis.first) continue;

      int absCoords = x + y * image.axis.first;
      if(image.masked(x, y, Image::badPixel) || image.masked(x, y, Image::psf))
        return 0.0;

      if(image[absCoords] >= args.threshHigh) {
        image.maskPix(x, y, Image::badPixel);
        return 0.0;
      }
      if((image[absCoords] - stamp.stats.skyEst) / stamp.stats.fwhm >
         args.threshKernFit)
        retVal += image[absCoords];
    }
  }
  return retVal;
}

inline cl_int findSStamps(Stamp& stamp, Image& image, int index) {
  cl_double floor = stamp.stats.skyEst + args.threshKernFit * stamp.stats.fwhm;

  cl_double dfrac = 0.9;
  while(stamp.subStamps.size() < size_t(args.maxSStamps)) {
    long absx, absy, coords;
    cl_double lowestPSFLim =
        std::max(floor, stamp.stats.skyEst +
                            (args.threshHigh - stamp.stats.skyEst) * dfrac);
    for(long x = 0; x < stamp.size.first; x++) {
      absx = x + stamp.coords.first;
      for(long y = 0; y < stamp.size.second; y++) {
        absy = y + stamp.coords.second;
        coords = x + (y * stamp.size.first);

        if(image.masked(absx, absy, Image::badPixel) ||
           image.masked(absx, absy, Image::psf) ||
           image.masked(absx, absy, Image::edge)) {
          continue;
        }

        if(stamp[coords] > args.threshHigh) {
          image.maskPix(absx, absy, Image::badPixel);
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
          long kCoords;
          for(long kx = absx - args.hSStampWidth; kx < absx + args.hSStampWidth;
              kx++) {
            if(kx < 0 || kx >= image.axis.first) continue;
            for(long ky = absy - args.hSStampWidth;
                ky < absy + args.hSStampWidth; ky++) {
              if(ky < 0 || ky >= image.axis.second) continue;
              kCoords = kx + (ky * image.axis.first);

              if(image[kCoords] >= args.threshHigh) {
                image.maskPix(kx, ky, Image::badPixel);
                continue;
              }

              if(image.masked(kx, ky, Image::badPixel) ||
                 image.masked(kx, ky, Image::psf) ||
                 image.masked(kx, ky, Image::edge)) {
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
          s.val = checkSStamp(s, image, stamp);
          if(s.val == 0) continue;
          stamp.subStamps.push_back(s);
          image.maskSStamp(s, Image::psf);
        }
        if(stamp.subStamps.size() >= size_t(args.maxSStamps)) break;
      }
      if(stamp.subStamps.size() >= size_t(args.maxSStamps)) break;
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
  std::sort(stamp.subStamps.begin(), stamp.subStamps.end(),
            std::greater<SubStamp>());
  if(args.verbose)
    std::cout << "Added " << stamp.subStamps.size() << " substamps to stamp "
              << index << std::endl;
  return 0;
}

inline void sigmaClip(std::vector<cl_double>& data, cl_double& mean,
                      cl_double& stdDev) {
  /* Does sigma clipping on data to provide the mean and stdDev of said
   * data
   */
  if(data.empty()) {
    std::cout << "Cannot send in empty vector to Sigma Clip" << std::endl;
    exit(1);
  }

  size_t currNPoints = 0;
  size_t prevNPoints = data.size();
  std::vector<bool> intMask(data.size(), false);

  // Do three times or a stable solution has been found.
  for(int i = 0; (i < 3) && (currNPoints != prevNPoints); i++) {
    currNPoints = prevNPoints;
    mean = 0;
    stdDev = 0;

    for(size_t i = 0; i < data.size(); i++) {
      if(!intMask[i]) {
        mean += data[i];
        stdDev += data[i] * data[i];
      }
    }

    if(prevNPoints > 1) {
      mean = mean / prevNPoints;
      stdDev = stdDev - prevNPoints * mean * mean;
      stdDev = std::sqrt(stdDev / double(prevNPoints - 1));
    } else {
      std::cout << "prevNPoints is: " << prevNPoints
                << "Needs to be greater than 1" << std::endl;
      exit(1);
    }

    prevNPoints = 0;
    double invStdDev = 1.0 / stdDev;
    for(size_t i = 0; i < data.size(); i++) {
      if(!intMask[i]) {
        // Doing the sigmaClip
        if(abs(data[i] - mean) * invStdDev > args.sigClipAlpha) {
          intMask[i] = true;
        } else {
          prevNPoints++;
        }
      }
    }
  }
}

inline void calcStats(Stamp& stamp, Image& image) {
  /* Heavily taken from HOTPANTS which itself copied it from Gary Bernstein
   * Calculates important values of stamps for futher calculations.
   * TODO: Fix Masking, very not correct. Also mask bad pixels found in here.
   * TODO: Compare results on same stamp on this and old version.
   */

  cl_double median, lfwhm, sum;  // temp for now

  std::vector<cl_double> values{};
  std::vector<cl_int> bins(256, 0);

  cl_int nValues = 100;
  cl_double upProc = 0.9;
  cl_double midProc = 0.5;
  cl_int numPix = stamp.size.first * stamp.size.second;

  if(numPix < nValues) {
    std::cout << "Not enough pixels in a stamp" << std::endl;
    exit(1);
  }

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<cl_int> randGenX(0, stamp.size.first - 1);
  std::uniform_int_distribution<cl_int> randGenY(0, stamp.size.second - 1);

  // Stop after randomly having selected a pixel numPix times.
  for(int i = 0; (i < numPix) && (values.size() < size_t(nValues)); i++) {
    int randX = randGenX(gen);
    int randY = randGenY(gen);

    // Random pixel in stamp in stamp coords.
    cl_int indexS = randX + randY * stamp.size.first;

    // Random pixel in stamp in Image coords.
    cl_int xI = randX + stamp.coords.first;
    cl_int yI = randY + stamp.coords.second;

    if(image.masked(xI, yI, Image::badInput) ||
       image.masked(xI, yI, Image::nan) || stamp[indexS] < 0) {
      continue;
    }

    values.push_back(stamp[indexS]);
  }

  std::sort(begin(values), end(values));

  // Width of a histogram bin.
  cl_double binSize = (values[(int)(upProc * values.size())] -
                       values[(int)(midProc * values.size())]) /
                      (cl_double)nValues;

  // Value of lowest bin.
  cl_double lowerBinVal =
      values[(int)(midProc * values.size())] - (128.0 * binSize);

  // Contains all good Pixels in the stamp, aka not masked.
  std::vector<cl_double> maskedStamp{};
  for(int x = 0; x < stamp.size.first; x++) {
    for(int y = 0; y < stamp.size.second; y++) {
      // Pixel in stamp in stamp coords.
      cl_int indexS = x + y * stamp.size.first;

      // Pixel in stamp in Image coords.
      cl_int xI = x + stamp.coords.first;
      cl_int yI = y + stamp.coords.second;

      if(!image.masked(xI, yI, Image::badInput) &&
         !image.masked(xI, yI, Image::nan) && stamp[indexS] >= 0) {
        maskedStamp.push_back(stamp[indexS]);
      }
    }
  }

  // sigma clip of maskedStamp to get mean and sd.
  cl_double mean, stdDev, invStdDev;
  sigmaClip(maskedStamp, mean, stdDev);
  invStdDev = 1.0 / stdDev;

  int attempts = 0;
  cl_int okCount = 0;
  cl_double sumBins = 0.0;
  cl_double sumExpect = 0.0;
  cl_double lower, upper;
  while(true) {
    if(attempts >= 5) {
      std::cout << "Creation of histogram unsuccessful after 5 attempts";
      return;
    }

    std::fill(bins.begin(), bins.end(), 0);
    okCount = 0;
    sum = 0.0;
    sumBins = 0.0;
    sumExpect = 0.0;
    for(int y = 0; y < stamp.size.second; y++) {
      for(int x = 0; x < stamp.size.first; x++) {
        // Pixel in stamp in stamp coords.
        cl_int indexS = x + y * stamp.size.first;

        // Pixel in stamp in Image coords.
        cl_int xI = x + stamp.coords.first;
        cl_int yI = y + stamp.coords.second;

        if(image.masked(xI, yI, Image::badInput) ||
           image.masked(xI, yI, Image::nan) || stamp[indexS] < 0) {
          continue;
        }

        if((std::abs(stamp[indexS] - mean) * invStdDev) > args.sigClipAlpha) {
          continue;
        }

        int index = std::clamp(
            (int)std::floor((stamp[indexS] - lowerBinVal) / binSize), 0, 255);

        bins[index]++;
        sum += abs(stamp[indexS]);
        okCount++;
      }
    }

    if(okCount == 0 || binSize == 0.0) {
      std::cout << "No good pixels or variation in pixels" << std::endl;
      exit(1);
    }

    cl_double maxDens = 0.0;
    int lowerIndex, upperIndex, maxIndex;
    for(lowerIndex = upperIndex = 1; upperIndex < 255;
        sumBins -= bins[lowerIndex++]) {
      while(sumBins < okCount / 10.0 && upperIndex < 255) {
        sumBins += bins[upperIndex++];
      }
      if(sumBins / (upperIndex - lowerIndex) > maxDens) {
        maxDens = sumBins / (upperIndex - lowerIndex);
        maxIndex = lowerIndex;
      }
    }
    if(maxIndex < 0 || maxIndex > 255) maxIndex = 0;

    sumBins = 0.0;
    for(int i = maxIndex; sumBins < okCount / 10.0 && i < 255; i++) {
      sumBins += bins[i];
      sumExpect += i * bins[i];
    }
    cl_double modeBin = sumExpect / sumBins + 0.5;
    stamp.stats.skyEst = lowerBinVal + binSize * (modeBin - 1.0);

    lower = okCount * 0.25;
    upper = okCount * 0.75;
    sumBins = 0.0;
    int i = 0;
    for(; sumBins < lower; sumBins += bins[i++])
      ;
    lower = i - (sumBins - lower) / bins[i - 1];
    for(; sumBins < upper; sumBins += bins[i++])
      ;
    upper = i - (sumBins - upper) / bins[i - 1];

    if(lower < 1.0 || upper > 255.0) {
      if(args.verbose) {
        std::cout << "Expanding bin size..." << std::endl;
      }
      lowerBinVal -= 128.0 * binSize;
      binSize *= 2;
      attempts++;
    } else if(upper - lower < 40.0) {
      if(args.verbose) {
        std::cout << "Shrinking bin size..." << std::endl;
      }
      binSize /= 3.0;
      lowerBinVal = stamp.stats.skyEst - 128.0 * binSize;
      attempts++;
    } else
      break;
  }
  stamp.stats.fwhm = binSize * (upper - lower) / args.iqRange;
  int i = 0;
  for(i = 0, sumBins = 0; sumBins < okCount / 2.0; sumBins += bins[i++])
    ;
  median = i - (sumBins - okCount / 2.0) / bins[i - 1];
  lfwhm = binSize * (median - lower) * 2.0 / args.iqRange;
  median = lowerBinVal + binSize * (median - 1.0);
}

inline int identifySStamps(std::vector<Stamp>& stamps, Image& image) {
  std::cout << "Identifying sub-stamps in " << image.name << "..." << std::endl;

  int index = 0, hasSStamps = 0;
  for(auto& s : stamps) {
    calcStats(s, image);
    findSStamps(s, image, index);
    if(!s.subStamps.empty()) hasSStamps++;
    index++;
  }
  return hasSStamps;
}

// TODO: can this live in the stamp struct?
inline void createB(Stamp& s, Image& img) {  // see Equation 2.13
  if(args.verbose) std::cout << "Creating B..." << std::endl;
  for(int i = 0; i < args.tmp_num_kernel_components; i++) {
    cl_double p0 = 0.0;
    for(int x = -args.hSStampWidth; x <= args.hSStampWidth; x++) {
      for(int y = -args.hSStampWidth; y <= args.hSStampWidth; y++) {
        int k = x + args.hSStampWidth +
                (args.hSStampWidth * 2) * (y + args.hSStampWidth);
        int imgIndex =
            x + s.coords.first + (y + s.coords.second) * s.size.first;
        p0 += s.W[i][k] * img[imgIndex];
      }
    }
    s.B[i + 1] = p0;
  }

  cl_double q = 0.0;
  for(int x = -args.hSStampWidth; x <= args.hSStampWidth; x++) {
    for(int y = -args.hSStampWidth; y <= args.hSStampWidth; y++) {
      int k = x + args.hSStampWidth +
              (args.hSStampWidth * 2) * (y + args.hSStampWidth);
      int imgIndex = x + s.coords.first + (y + s.coords.second) * s.size.first;
      q += s.W[args.tmp_num_kernel_components][k] * img[imgIndex];
    }
  }
  s.B[args.tmp_num_kernel_components + 1] = q;
}

inline void convStamp(Stamp& s, Image& img, Kernel& k, int n, int odd) {
  if(args.verbose) std::cout << "Convolving stamp..." << std::endl;

  s.W.emplace_back();
  cl_long ssx = s.subStamps[0].imageCoords.first;
  cl_long ssy = s.subStamps[0].imageCoords.second;

  std::vector<cl_double> tmp{};

  int totWidth = args.hSStampWidth + args.hKernelWidth - 1;
  for(int i = ssx - args.hSStampWidth - args.hKernelWidth;
      i <= ssx + args.hSStampWidth + args.hKernelWidth; i++) {
    for(int j = ssy - args.hSStampWidth; j <= ssy + args.hSStampWidth; j++) {
      int index =
          i - ssx + totWidth / 2 + totWidth * (j - ssy + args.hSStampWidth);
      tmp.push_back(0.0);
      if(index != tmp.size() - 1)
        std::cout << "index is: " << index << ", tmp length is: " << tmp.size()
                  << std::endl;
      for(int y = -args.hKernelWidth; y <= args.hKernelWidth; y++) {
        std::cout << "i";
        int imgIndex = i + (j + y) * img.axis.first;
        tmp[index] += img[imgIndex] * k.filterY[n][args.hKernelWidth - y];
      }
    }
  }
  std::cout << "After loop 1" << std::endl;

  for(int j = -args.hSStampWidth; j <= args.hSStampWidth; j++) {
    for(int i = -args.hSStampWidth; i <= args.hSStampWidth; i++) {
      int index =
          i + args.hSStampWidth + (j + args.hSStampWidth) * args.fSStampWidth;
      s.W[n].push_back(0.0);
      for(int x = -args.hKernelWidth; x <= args.hKernelWidth; x++) {
        int imgIndex = i + (j + x) * img.axis.first;
        s.W[n][index] +=
            tmp[i + x + totWidth / 2 + (j + args.hSStampWidth) * totWidth] *
            k.filterX[n][args.hKernelWidth - x];
      }
    }
  }
  std::cout << "After loop 2" << std::endl;

  if(odd) {
    for(int i = 0; i < args.fSStampWidth * args.fSStampWidth; i++)
      s.W[n][i] -= s.W[0][i];
  }
}

inline void cutSStamp(SubStamp& ss, Image& img) {
  if(args.verbose) std::cout << "Cutting substamp..." << std::endl;
  ss.init();
  int ssx = ss.imageCoords.first;
  int ssy = ss.imageCoords.second;
  for(int y = 0; y <= args.fSStampWidth; y++) {
    int imgY = ssy + y - args.hSStampWidth;
    for(int x = 0; x <= args.fSStampWidth; x++) {
      int imgX = ssx + x - args.hSStampWidth;
      ss[x + y * args.fSStampWidth] = img[imgX + imgY * img.axis.first];
      ss.sum += img.masked(imgX, imgY, Image::badInput) ||
                        img.masked(imgX, imgY, Image::nan)
                    ? 0.0
                    : abs(img[imgX + imgY * img.axis.first]);
    }
  }
}

inline void fillStamp(Stamp& s, Image& tImg, Image& sImg, Kernel& k) {
  if(args.verbose) std::cout << "Filling stamp..." << std::endl;
  if(s.subStamps.empty()) {
    if(args.verbose)
      std::cout << "No eligable substamps, stamp rejected" << std::endl;
    return;
  }

  int nvec = 0;
  for(int g = 0; g < args.dg.size(); g++) {
    for(int x = 0; x <= args.dg[g]; x++) {
      for(int y = 0; y <= args.dg[g] - x; y++) {
        int odd = 0;

        cl_double dx = (x / 2) * 2 - x;
        cl_double dy = (y / 2) * 2 - y;
        if(dx == 0 && dy == 0 && nvec > 0) odd = 1;

        convStamp(s, tImg, k, nvec, odd);
        std::cout << "Stamp convolved" << std::endl;
        nvec++;
      }
    }
  }

  cutSStamp(s.subStamps[0], sImg);

  cl_long ssx = s.subStamps[0].imageCoords.first;
  cl_long ssy = s.subStamps[0].imageCoords.second;
  for(int x = ssx - args.hSStampWidth; x <= ssx + args.hSStampWidth; x++) {
    for(int y = ssy - args.hSStampWidth; y <= ssy + args.hSStampWidth; y++) {
      cl_double ax = 1.0;
      int n = nvec;
      for(int j = 0; j <= args.backgroundOrder; j++) {
        cl_double ay = 1.0;
        for(int k = 0; k <= args.backgroundOrder - j; k++) {
          s.W[n++][x - (ssx - args.hSStampWidth) +
                   (y - (ssy - args.hSStampWidth)) * args.fSStampWidth] =
              ax * ay;
          ay *= (y - tImg.axis.second * 0.5) / tImg.axis.second * 0.5;
        }
        ax *= (x - tImg.axis.first * 0.5) / tImg.axis.first * 0.5;
      }
    }
  }

  s.createQ();  // TODO: is name accurate?
  createB(s, sImg);
}

#endif
