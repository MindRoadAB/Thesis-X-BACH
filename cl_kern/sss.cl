#define MASK_BAD_PIX_VAL (1 << 0)
#define MASK_SAT_PIXEL (1 << 1)
#define MASK_LOW_PIXEL (1 << 2)
#define MASK_NAN_PIXEL (1 << 3)
#define MASK_BAD_CONV (1 << 4)
#define MASK_INPUT_MASK (1 << 5)
#define MASK_OK_CONV (1 << 6)
#define MASK_BAD_INPUT (1 << 7)
#define MASK_BAD_PIXEL_T (1 << 8)
#define MASK_SKIP_T (1 << 9)
#define MASK_BAD_PIXEL_S (1 << 10)
#define MASK_SKIP_S (1 << 11)
#define MASK_BAD_OUTPUT (1 << 12)

#define ZEROVAL ((double)1e-10)

#define STATS_SIZE (5)
#define STAT_SKY_EST (0)
#define STAT_FWHM (1)
#define STAT_NORM (2)
#define STAT_DIFF (3)
#define STAT_CHI2 (4)

#define PRINT_STAMP 41

//TODO: Fix swizzling for stamp data

void kernel createStampBounds(global long *stampsCoords, global long *stampsSizes,
                              const int nStampsX, const int nStampsY, const long fullStampWidth, 
                              const long w, const long h) {
    const int id = get_global_id(0);
    const int stampX = id % nStampsX;
    const int stampY = id / nStampsX;


    long startX = stampX * w / nStampsX;
    long startY = stampY * h / nStampsY;
    
    long stopX = min(startX + fullStampWidth, w);
    long stopY = min(startY + fullStampWidth, h);

    long stampW = stopX - startX;
    long stampH = stopY - startY;

    stampsCoords[2*id + 0] = startX;
    stampsCoords[2*id + 1] = startY;
    stampsSizes[2*id + 0] = stampW;
    stampsSizes[2*id + 1] = stampH;
}
double checkSStamp(global const double *img, global ushort *mask,
                   const double skyEst, const double fwhm, const long imgW,
                   const int2 sstampCoords, const long hSStampWidth,
                   const long2 stampCoords, const long2 stampSize,
                   const double threshHigh, const double threshKernFit,
                   const ushort badMask, const ushort badPixelMask) {
    double retVal = 0.0;
    int stamp = get_global_id(0);

    long startX = max(sstampCoords.x - hSStampWidth, stampCoords.x);
    long startY = max(sstampCoords.y - hSStampWidth, stampCoords.y);
    long endX   = min(sstampCoords.x + hSStampWidth, stampCoords.x + stampSize.x - 1);
    long endY   = min(sstampCoords.y + hSStampWidth, stampCoords.y + stampSize.y - 1);
    
    // if (stamp == PRINT_STAMP) {
    //     printf("Checking (%ld, ", sstampCoords.x);
    //     printf("%ld): ", sstampCoords.y);
    // }
    for(int y = startY; y <= endY; y++) {
        for(int x = startX; x <= endX; x++) {
            
            int absCoords = x + y * imgW;
            // if (x == 305 && y == 87) printf("%hd ", mask[absCoords]);
            if ((mask[absCoords] & badMask) > 0) {
                // if (stamp == PRINT_STAMP){
                //     printf("Has overlap/bad pixel at (%d, ", x);
                //     printf("%d)\n", y);
                // }
                return 0.0;
            }

            double imgValue = img[absCoords];
            if(imgValue > threshHigh) {
                // if (stamp == PRINT_STAMP){
                //     printf("Has NEW bad pixel at (%d, ", x);
                //     printf("%d)\n", y);
                // }
                mask[absCoords] |= badPixelMask;
                return 0.0;
            }
            
            double kernFit = (imgValue - skyEst) / fwhm;
            if((imgValue - skyEst) / fwhm > threshKernFit) {
                retVal += imgValue;
            }
        }
    }
    // if (stamp == PRINT_STAMP ){
    //     if (retVal > 0.0) printf("Clean\n");
    //     else printf("Not above threshKernFit\n");
    // }
    return retVal;
}

void sortSubStamps(int substampCount, local int2 *coords, local double *values)
{
    int i = 1;
    while (i < substampCount) {
        int j = i;
        while ((j > 0) && (values[j-1] < values[j])){
            int2 tmpCoords = coords[j-1];
            coords[j-1] = coords[j];
            coords[j] = tmpCoords;
            double tmpVal = values[j-1];
            values[j-1] = values[j];
            values[j] = tmpVal;
            j--;
        }
        i++;
    }
}

void kernel findSubStamps(global const double* img, global ushort *mask, 
                          global const long2 *stampsCoords, global const long2 *stampsSizes,
                          global const double *stampsStats,
                          global int2 *sstampsCoords, global double *sstampsValues,
                          global int *sstampsCounts,
                          const double threshHigh, const double threshKernFit,
                          const long imgW, const int fStampWidth, const int hSStampWidth,
                          const int maxSStamps, const int maxStamps, const ushort badMask, const ushort badPixelMask, const ushort skipMask,
                          local int2 *localSubStampCoords, local double *localSubStampValues) {
    int stamp      = get_global_id(0);
    int localStamp = get_local_id(0);
    if (stamp >= maxStamps) return;

    double skyEst = stampsStats[STATS_SIZE * stamp + STAT_SKY_EST];
    double fwhm = stampsStats[STATS_SIZE * stamp + STAT_FWHM];

    // if(stamp == PRINT_STAMP){
    //     if (skyEst == 0) printf("bad skyEst\n");
    //     if (fwhm == 0.) printf("bad FWHM\n");
    // }

    double floor = skyEst + threshKernFit * fwhm;
    double dfrac = 0.9;
    
    long2 stampCoords = stampsCoords[stamp];
    long2 stampSize =  stampsSizes[stamp];

    int sstampCounter = 0;
    // if (stamp == PRINT_STAMP){
    //     printf("stamp %d: (", stamp);
    //     printf("%d-", stampCoords.x);
    //     printf("%d, ", stampCoords.x+stampSize.x);
    //     printf("%d-", stampCoords.y);
    //     printf("%d)\n", stampCoords.y+stampSize.y);
    // }
    while(sstampCounter < maxSStamps) {
        double lowestPSFLim = max(floor, skyEst + (threshHigh - skyEst) * dfrac);
        for(long y = 0; y < fStampWidth; y++) {
            long absy = y + stampCoords.y;
            for(long x = 0; x < fStampWidth; x++) {
                long absx = x + stampCoords.x;
                long absCoords  = absx + (absy * imgW);
                
                if ((mask[absCoords] & badMask) > 0) {
                    continue;
                }

                double imgValue = img[absCoords];
                if(imgValue > threshHigh) {
                    mask[absCoords] |= badPixelMask;
                    continue;
                }
                
                
                // if (absx <= 1856 && absx >= 1841 && absy == 506){
                //     printf("(%d, ", absx);
                //     printf("%d): kernFit: ", absy);
                //     printf("%f\n", (imgValue - skyEst) * (1.0 / fwhm));
                // }
                if((imgValue - skyEst) * (1.0 / fwhm) < threshKernFit) {
                    continue;
                }

                if(imgValue > lowestPSFLim) {  // good candidate found
                    double maxVal = 0;
                    int2  maxCoords;
                    maxCoords.x = (int)absx;
                    maxCoords.y = (int)absy;
                    long startX = max(absx - hSStampWidth, stampCoords.x);
                    long startY = max(absy - hSStampWidth, stampCoords.y);
                    long endX   = min(absx + hSStampWidth, stampCoords.x + fStampWidth - 1);
                    long endY   = min(absy + hSStampWidth, stampCoords.y + fStampWidth - 1);
                    // if (stamp == PRINT_STAMP){
                    //     printf("candidate at (%ld", startX);
                    //     printf("-%ld, ", endX);
                    //     printf("%ld", startY);
                    //     printf("-%ld)\n", endY);
                    // }
                    
                    for(long ky = startY; ky <= endY; ky++) {
                        for(long kx = startX; kx <= endX; kx++) {
                            
                            long kCoords = kx + (ky * imgW);
                            double kImgValue = img[kCoords];
                            if ((mask[kCoords] & badMask) > 0) {
                                continue;
                            }

                            if(kImgValue >= threshHigh) {
                                mask[kCoords] |= badPixelMask;
                                continue;
                            }
                            if((kImgValue - skyEst) * (1.0 / fwhm) < threshKernFit) {
                                continue;
                            }

                            if(kImgValue > maxVal) {
                                maxVal = kImgValue;
                                maxCoords.x = (int)kx;
                                maxCoords.y = (int)ky;
                            }
                        }
                    }
                    
                    maxVal = checkSStamp(img, mask, skyEst, fwhm, imgW,
                                         maxCoords, hSStampWidth,
                                         stampCoords, stampSize,
                                         threshHigh, threshKernFit,
                                         badMask, badPixelMask);
                    
                    if(maxVal == 0.0) continue;
                
                    // if (stamp == PRINT_STAMP){
                    //     printf("new substamp at (%ld, ", maxCoords.x);
                    //     printf("%ld): ", maxCoords.y);
                    //     printf("%f\n", maxVal);
                    // }

                    localSubStampCoords[localStamp * maxSStamps + sstampCounter] = maxCoords;
                    localSubStampValues[localStamp * maxSStamps + sstampCounter] = maxVal;
                    sstampCounter++;
    
                    long startX2 = max((long)(maxCoords.x - hSStampWidth), stampCoords.x);
                    long startY2 = max((long)(maxCoords.y - hSStampWidth), stampCoords.y);
                    long endX2 = min((long)(maxCoords.x + hSStampWidth), stampCoords.x + stampSize.x - 1);
                    long endY2 = min((long)(maxCoords.y + hSStampWidth), stampCoords.y + stampSize.y - 1);

                    for(int y = startY2; y <= endY2; y++) {
                        for(int x = startX2; x <= endX2; x++) {
                            mask[x + y*imgW] |= skipMask;
                        }
                    }
                }
                if(sstampCounter >= maxSStamps) break;
            }
            if(sstampCounter >= maxSStamps) break;
        }
        if(lowestPSFLim == floor) break;
        dfrac -= 0.2;
    }
        
    sortSubStamps(sstampCounter, 
        &localSubStampCoords[localStamp*maxSStamps], 
        &localSubStampValues[localStamp*maxSStamps]);

    sstampsCounts[stamp] = min(sstampCounter, maxSStamps / 2);
    for(int i = 0; i < sstampCounter; i++) {
        sstampsCoords[stamp * maxSStamps + i] = localSubStampCoords[localStamp*maxSStamps + i];
        sstampsValues[stamp * maxSStamps + i] = localSubStampValues[localStamp*maxSStamps + i];
    }
    for (int i = sstampCounter; i<maxSStamps; i++) {
        sstampsCoords[stamp * maxSStamps + i] = (int2)(INT_MAX, INT_MAX);
        sstampsValues[stamp * maxSStamps + i] = -INFINITY;
    }
}

void kernel markStampsToKeep(global const int *sstampCounts, global int *keepIndeces, global int *keepCounter){
    int stamp = get_global_id(0);
    if (sstampCounts[stamp] > 0) {
        keepIndeces[atomic_inc(keepCounter)] = stamp;
    }
}

void kernel removeEmptyStamps(global const long2 *stampCoords, global const long2 *stampSizes,
                              global const double *stampStats, global const int *subStampCounts,
                              global const int2 *subStampCoords, global const double *subStampValues,
                              global long2 *filteredStampCoords, global long2 *filteredStampSizes,
                              global double *filteredStampStats, global int *filteredSubStampCounts,
                              global int2 *filteredSubStampCoords, global double *filteredSubStampValues,
                              global const int *keepIndeces, global const int *keepCounter, int maxKSStamps) {
    int stamp = get_global_id(0);
    if(stamp >= *keepCounter) return;
    
    int index = keepIndeces[stamp];
    filteredStampCoords[stamp] = stampCoords[index];
    filteredStampSizes[stamp] = stampSizes[index];
    filteredStampStats[STATS_SIZE * stamp + STAT_SKY_EST] = stampStats[STATS_SIZE * index + STAT_SKY_EST];
    filteredStampStats[STATS_SIZE * stamp + STAT_FWHM] = stampStats[STATS_SIZE * index + STAT_FWHM];
    filteredStampStats[STATS_SIZE * stamp + STAT_NORM] = stampStats[STATS_SIZE * index + STAT_NORM];
    filteredStampStats[STATS_SIZE * stamp + STAT_DIFF] = stampStats[STATS_SIZE * index + STAT_DIFF];
    filteredStampStats[STATS_SIZE * stamp + STAT_CHI2] = stampStats[STATS_SIZE * index + STAT_CHI2];
    
    filteredSubStampCounts[stamp] = subStampCounts[index];
    
    int sstampCount = subStampCounts[index];
    int maxSStamps = 2 * maxKSStamps;
    for(int i = 0; i < sstampCount; i++){
        filteredSubStampCoords[maxSStamps*stamp + i] = subStampCoords[maxSStamps*index + i];
        filteredSubStampValues[maxSStamps*stamp + i] = subStampValues[maxSStamps*index + i];
    }
} 