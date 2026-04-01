#include "stdafx.h"
#include "SpatialRadBA.h"
#include "OlpFile.hpp"
#include "WuMath.hpp"
#include "WuLog.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <map>
#include <vector>
#include <algorithm>

#ifdef _OPENMP
#include <omp.h>
#endif

#ifndef DOS_PATH
#define DOS_PATH(x) for(char*p=x;*p;p++)if(*p=='/')* p='\\'
#endif

// ==================== IGG3 权函数 ====================
static inline double IGG3Weight(double std_res, double init_w,
    double k0 = 3.0, double k1_thr = 6.0)
{
    if (std_res <= k0)
        return init_w;
    else if (std_res <= k1_thr) {
        double ratio = (k1_thr - std_res) / (k1_thr - k0);
        return init_w * ratio * ratio;
    }
    else
        return 0.0;
}

// ==================== SpatialRadParams ====================

SpatialRadParams::SpatialRadParams() {
    s0 = 1.0; sx = sy = sxx = sxy = syy = 0.0;
    a00 = a0x = a0y = a0xx = a0xy = a0yy = 0.0;
    a10 = a1x = a1y = 0.0;
    a20 = a2x = a2y = 0.0;
    imgWidth = imgHeight = 1000;
}

void SpatialRadParams::initFromGlobal(double s, double a0, double a1, double a2) {
    s0 = s; sx = sy = sxx = sxy = syy = 0.0;
    a00 = a0; a0x = a0y = a0xx = a0xy = a0yy = 0.0;
    a10 = a1; a1x = a1y = 0.0;
    a20 = a2; a2x = a2y = 0.0;
}

// ==================== SpatialRadBA ====================

SpatialRadBA::SpatialRadBA() {
    m_useSpatial = true; m_smoothWeight = 0.05; m_maxIter = 5;
    m_finalRMSE = 0.0; m_iterations = 0;
}
SpatialRadBA::~SpatialRadBA() {}


// ---------- 收集olp文件列表（基础版，用于 initializeGlobal）----------
// 始终读取原始 .olp 文件（无质量分级），仅用于统计观测数量
static void CollectOlpList(int numImages, const char* const* tskFiles,
    std::vector<SpatialRadBA::OlpInfo>& olpList)
{
    olpList.clear();
    olpList.reserve(numImages * 10);
    for (int imgIdx = 0; imgIdx < numImages; imgIdx++) {
        char str[512], strSrc[256], strRef[256], strOlp[512];
        int idx, idxr;
        FILE* fTsk = fopen(tskFiles[imgIdx], "rt");
        if (!fTsk) continue;
        fgets(str, 512, fTsk); sscanf(str, "%s", strSrc); DOS_PATH(strSrc);
        fgets(str, 512, fTsk); sscanf(str, "%d", &idx);
        // 跳过LAS行
        {
            long pos = ftell(fTsk);
            while (fgets(str, 512, fTsk)) {
                char tmp[512] = {}; sscanf(str, "%s", tmp);
                if (_strnicmp(tmp, "LAS=", 4) == 0 || _strnicmp(tmp, "LAS:", 4) == 0 ||
                    _strnicmp(tmp, "CPT=", 4) == 0) pos = ftell(fTsk);
                else { fseek(fTsk, pos, SEEK_SET); break; }
            }
        }
        while (!feof(fTsk)) {
            if (!fgets(str, 512, fTsk)) break;
            sscanf(str, "%s", strRef); DOS_PATH(strRef);
            if (!fgets(str, 512, fTsk)) break;
            sscanf(str, "%d", &idxr);
            strcpy(strOlp, tskFiles[imgIdx]);
            strcpy(strrchr(strOlp, '.'), "_");
            strcat(strOlp, strrchr(strRef, '\\') + 1);
            strcat(strOlp, ".olp");
            SpatialRadBA::OlpInfo info; strcpy(info.path, strOlp);
            info.idx = idx; info.idxr = idxr;
            info.initWeight = (idxr == -1) ? 1.0 : 0.5;  // 默认权重
            olpList.push_back(info);
        }
        fclose(fTsk);
    }
}

// ---------- 收集质量分级olp文件列表（供 solveIteration 使用）----------
// 优先读取质量分级文件（_excellent/_good/_fair/_poor.olp），
// 若质量文件均不存在则回退到原始 .olp 文件（向后兼容）。
static void CollectOlpListWithQuality(
    int numImages,
    const char* const* tskFiles,
    const std::vector<const char*>& excellentFiles,
    const std::vector<const char*>& goodFiles,
    const std::vector<const char*>& fairFiles,
    const std::vector<const char*>& poorFiles,
    std::vector<SpatialRadBA::OlpInfo>& olpList)
{
    olpList.clear();

    bool useQuality = !excellentFiles.empty() || !goodFiles.empty() ||
        !fairFiles.empty() || !poorFiles.empty();

    if (!useQuality) {
        // 向后兼容：没有质量文件则读原始 .olp
        CollectOlpList(numImages, tskFiles, olpList);
        return;
    }

    // ── 按质量等级逐文件收集 ──
    // 质量文件按 (idxr, 质量等级) 区分，需要恢复 idx/idxr 信息。
    // 质量文件路径命名规则与 MchTie 中完全一致：
    //   原始 .olp  →  _excellent.olp / _good.olp / _fair.olp / _poor.olp
    // 因此可以从质量文件路径反推出对应的 idx/idxr：
    //   遍历任务文件，对每对(src, ref)构造原始 .olp 路径，
    //   再派生出质量文件路径，若存在则加入列表。

    struct QualEntry { const std::vector<const char*>* files; double w; const char* suffix; };
    QualEntry quals[] = {
        { &excellentFiles, 1.00, "_excellent.olp" },
        { &goodFiles,      0.80, "_good.olp"      },
        { &fairFiles,      0.80, "_fair.olp"      },
        { &poorFiles,      0.20, "_poor.olp"      },
    };

    for (int imgIdx = 0; imgIdx < numImages; imgIdx++) {
        char str[512], strSrc[256], strRef[256], strOlp[512];
        int idx, idxr;
        FILE* fTsk = fopen(tskFiles[imgIdx], "rt");
        if (!fTsk) continue;
        fgets(str, 512, fTsk); sscanf(str, "%s", strSrc); DOS_PATH(strSrc);
        fgets(str, 512, fTsk); sscanf(str, "%d", &idx);
        // 跳过LAS等参数行
        {
            long pos = ftell(fTsk);
            while (fgets(str, 512, fTsk)) {
                char tmp[512] = {}; sscanf(str, "%s", tmp);
                if (_strnicmp(tmp, "LAS=", 4) == 0 || _strnicmp(tmp, "LAS:", 4) == 0 ||
                    _strnicmp(tmp, "CPT=", 4) == 0) pos = ftell(fTsk);
                else { fseek(fTsk, pos, SEEK_SET); break; }
            }
        }
        while (!feof(fTsk)) {
            if (!fgets(str, 512, fTsk)) break;
            sscanf(str, "%s", strRef); DOS_PATH(strRef);
            if (!fgets(str, 512, fTsk)) break;
            sscanf(str, "%d", &idxr);

            // 构造原始 .olp 基础路径
            strcpy(strOlp, tskFiles[imgIdx]);
            strcpy(strrchr(strOlp, '.'), "_");
            strcat(strOlp, strrchr(strRef, '\\') + 1);
            strcat(strOlp, ".olp");

            // 对每个质量等级派生路径并检查是否存在
            bool anyQualFound = false;
            for (auto& qe : quals) {
                // 从基础路径派生质量文件路径
                char qualPath[512];
                strcpy(qualPath, strOlp);
                char* pExt = strrchr(qualPath, '.');
                if (pExt) strcpy(pExt, qe.suffix);

                // 只有该路径确实在传入的质量文件列表中时才收录
                // （确保路径与 RadCaliDlg 中构造的完全一致）
                bool inList = false;
                for (const char* fp : *qe.files) {
                    if (fp && _stricmp(fp, qualPath) == 0) { inList = true; break; }
                }
                if (!inList) continue;

                // 检查文件确实存在
                FILE* ft = fopen(qualPath, "rb");
                if (!ft) continue;
                fclose(ft);

                SpatialRadBA::OlpInfo info;
                strcpy(info.path, qualPath);
                info.idx = idx;
                info.idxr = idxr;
                info.initWeight = qe.w * ((idxr == -1) ? 1.0 : 0.5);
                olpList.push_back(info);
                anyQualFound = true;
            }

            // 如果该配对完全没有质量文件，回退到原始 .olp（保证不丢数据）
            if (!anyQualFound) {
                FILE* ft = fopen(strOlp, "rb");
                if (ft) {
                    fclose(ft);
                    SpatialRadBA::OlpInfo info;
                    strcpy(info.path, strOlp);
                    info.idx = idx;
                    info.idxr = idxr;
                    info.initWeight = (idxr == -1) ? 1.0 : 0.5;
                    olpList.push_back(info);
                }
            }
        }
        fclose(fTsk);
    }
}

// ---------- initializeGlobal ----------
bool SpatialRadBA::initializeGlobal(int numImages, SpatialRadParams* pParams)
{
    for (int i = 0; i < numImages; i++) pParams[i].initFromGlobal(1.0, 0.0, 0.0, 0.0);
    return true;
}

// ---------- solveIteration ----------
// obsWeights:  当前各观测点权重（跨迭代持久，在solveBand层面维护）
// initWeights: 各点初始权重上限（绝对约束1.0，相对约束0.1），第一次调用时初始化
bool SpatialRadBA::solveIteration(int numImages, int band, const char* const* tskFiles,
    const char* strRom, SpatialRadParams* pParams,
    std::vector<double>& obsWeights,
    std::vector<double>& initWeights,
    double& rmse)
{
    const std::vector<OlpInfo>& olpList = m_cachedOlpList;
    const std::vector<int>& fileOidStart = m_cachedFileOidStart;

    const int paramsPerImg = 18;
    const int n = numImages * paramsPerImg;

    int numOlpFiles = (int)olpList.size();

    int totalObs = (int)obsWeights.size();  // 已在 solveBand 中初始化

    // ------ 建立法方程（OpenMP 并行，每线程独立 localMap + localAtWb）------
    // 注意：obsWeights 按 olpList 的文件顺序、文件内观测点顺序排列。
    // 并行时每个线程处理一个 olp 文件（schedule dynamic），需要知道该文件
    // 在 obsWeights 中的起始偏移。预先计算各文件的起始偏移。
    std::map<std::pair<int, int>, double> gMap;
    Eigen::VectorXd gAtWb = Eigen::VectorXd::Zero(n);
    double sumSqRes = 0.0; int validObs = 0;

#ifdef _OPENMP
    omp_lock_t writeLock;
    omp_init_lock(&writeLock);
#pragma omp parallel
    {
        std::map<std::pair<int, int>, double> localMap;
        Eigen::VectorXd localAtWb = Eigen::VectorXd::Zero(n);
        double localSumSqRes = 0.0; int localValidObs = 0;
#pragma omp for schedule(dynamic,1) nowait
        for (int fi = 0; fi < numOlpFiles; fi++) {
#else
            {
                std::map<std::pair<int, int>, double> localMap;
                Eigen::VectorXd localAtWb = Eigen::VectorXd::Zero(n);
                double localSumSqRes = 0.0; int localValidObs = 0;
                for (int fi = 0; fi < numOlpFiles; fi++) {
#endif
                    const OlpInfo& info = olpList[fi];
                    COlpFile olpF; if (!olpF.Load4File(info.path)) continue;
                    int oz; OBV* pOs = olpF.GetData(&oz);
                    int idx = info.idx, idxr = info.idxr;
                    int oidBase = fileOidStart[fi];

                    for (int vi = 0; vi < oz; vi++, pOs++) {
                        int oid = oidBase + vi;
                        double w = (oid < (int)obsWeights.size()) ? obsWeights[oid] : 0.0;
                        if (w < 1e-6) continue;

                        double x1 = pOs->cc, y1 = pOs->cr;
                        const SpatialRadParams& p1 = pParams[idx];
                        double xn1 = p1.normX(x1), yn1 = p1.normY(y1);
                        double k1 = getKval(1, pOs->csz, pOs->cvz, pOs->cas);
                        double k2 = getKval(4, pOs->csz, pOs->cvz, pOs->cas);
                        double cv = pOs->cv[band];
                        double s1 = p1.getS(x1, y1), a0_1 = p1.getA0(x1, y1);
                        double a1_1 = p1.getA1(x1, y1), a2_1 = p1.getA2(x1, y1);
                        int bi = idx * paramsPerImg;

                        if (idxr == -1) {
                            w = 1.0;
                            double rv = pOs->rv[band];
                            double v = s1 * cv - (a0_1 + k1 * a1_1 + k2 * a2_1) - rv;
                            localSumSqRes += v * v; localValidObs++;

                            double a[18];
                            a[0] = cv;          a[1] = cv * xn1;       a[2] = cv * yn1;
                            a[3] = cv * xn1 * xn1;  a[4] = cv * xn1 * yn1;  a[5] = cv * yn1 * yn1;
                            a[6] = -1.0; a[7] = -xn1; a[8] = -yn1;
                            a[9] = -xn1 * xn1; a[10] = -xn1 * yn1; a[11] = -yn1 * yn1;
                            a[12] = -k1; a[13] = -k1 * xn1; a[14] = -k1 * yn1;
                            a[15] = -k2; a[16] = -k2 * xn1; a[17] = -k2 * yn1;

                            for (int ii = 0; ii < 18; ii++) {
                                localAtWb(bi + ii) -= w * a[ii] * v;
                                for (int jj = 0; jj < 18; jj++)
                                    localMap[{bi + ii, bi + jj}] += w * a[ii] * a[jj];
                            }
                        }
                        else if (idxr >= 0 && idxr < numImages) {
                            double x2 = pOs->rc, y2 = pOs->rr;
                            const SpatialRadParams& p2 = pParams[idxr];
                            double xn2 = p2.normX(x2), yn2 = p2.normY(y2);
                            double k1r = getKval(1, pOs->rsz, pOs->rvz, pOs->ras);
                            double k2r = getKval(4, pOs->rsz, pOs->rvz, pOs->ras);
                            double rv = pOs->rv[band];
                            double s2 = p2.getS(x2, y2), a0_2 = p2.getA0(x2, y2);
                            double a1_2 = p2.getA1(x2, y2), a2_2 = p2.getA2(x2, y2);
                            double gb1 = s1 * cv - (a0_1 + k1 * a1_1 + k2 * a2_1);
                            double gb2 = s2 * rv - (a0_2 + k1r * a1_2 + k2r * a2_2);
                            double v = gb1 - gb2;
                            localSumSqRes += v * v; localValidObs++;

                            int bj = idxr * paramsPerImg;
                            double ai[18], aj[18];
                            ai[0] = cv;          ai[1] = cv * xn1;      ai[2] = cv * yn1;
                            ai[3] = cv * xn1 * xn1;  ai[4] = cv * xn1 * yn1; ai[5] = cv * yn1 * yn1;
                            ai[6] = -1.0; ai[7] = -xn1; ai[8] = -yn1;
                            ai[9] = -xn1 * xn1; ai[10] = -xn1 * yn1; ai[11] = -yn1 * yn1;
                            ai[12] = -k1; ai[13] = -k1 * xn1; ai[14] = -k1 * yn1;
                            ai[15] = -k2; ai[16] = -k2 * xn1; ai[17] = -k2 * yn1;

                            aj[0] = -rv;          aj[1] = -rv * xn2;     aj[2] = -rv * yn2;
                            aj[3] = -rv * xn2 * xn2;  aj[4] = -rv * xn2 * yn2; aj[5] = -rv * yn2 * yn2;
                            aj[6] = 1.0; aj[7] = xn2; aj[8] = yn2;
                            aj[9] = xn2 * xn2; aj[10] = xn2 * yn2; aj[11] = yn2 * yn2;
                            aj[12] = k1r; aj[13] = k1r * xn2; aj[14] = k1r * yn2;
                            aj[15] = k2r; aj[16] = k2r * xn2; aj[17] = k2r * yn2;

                            for (int ii = 0; ii < 18; ii++) {
                                localAtWb(bi + ii) -= w * ai[ii] * v;
                                localAtWb(bj + ii) -= w * aj[ii] * v;
                            }
                            for (int ii = 0; ii < 18; ii++) for (int jj = 0; jj < 18; jj++) {
                                localMap[{bi + ii, bi + jj}] += w * ai[ii] * ai[jj];
                                localMap[{bj + ii, bj + jj}] += w * aj[ii] * aj[jj];
                                localMap[{bi + ii, bj + jj}] += w * ai[ii] * aj[jj];
                                localMap[{bj + ii, bi + jj}] += w * aj[ii] * ai[jj];
                            }
                        }
                    }
                } // fi

#ifdef _OPENMP
                omp_set_lock(&writeLock);
#endif
                for (auto& e : localMap) gMap[e.first] += e.second;
                gAtWb += localAtWb;
                sumSqRes += localSumSqRes;
                validObs += localValidObs;
#ifdef _OPENMP
                omp_unset_lock(&writeLock);
            } // end parallel
            omp_destroy_lock(&writeLock);
#else
        } // end block
#endif

    // ------ 平滑正则化 ------
            //if (m_smoothWeight > 0.0) {
            //    double wS = m_smoothWeight;
            //    double wS1 = m_smoothWeight * 1.0;  // ✅ 一阶系数权重降低
            //    double wS2 = m_smoothWeight * 3.0;  // ✅ 二阶系数权重提高

            //    for (int imgIdx = 0; imgIdx < numImages; imgIdx++) {
            //        int base = imgIdx * paramsPerImg;

            //        // 一阶系数：较弱平滑
            //        gMap[{base + 1, base + 1}] += wS1;  // sx
            //        gMap[{base + 2, base + 2}] += wS1;  // sy
            //        gMap[{base + 7, base + 7}] += wS1;  // a0x
            //        gMap[{base + 8, base + 8}] += wS1;  // a0y
            //        gMap[{base + 13, base + 13}] += wS1; // a1x
            //        gMap[{base + 14, base + 14}] += wS1; // a1y
            //        gMap[{base + 16, base + 16}] += wS1; // a2x
            //        gMap[{base + 17, base + 17}] += wS1; // a2y

            //        // 二阶系数：强平滑
            //        gMap[{base + 3, base + 3}] += wS2;   // sxx
            //        gMap[{base + 4, base + 4}] += wS;    // sxy
            //        gMap[{base + 5, base + 5}] += wS2;   // syy
            //        gMap[{base + 9, base + 9}] += wS2;   // a0xx
            //        gMap[{base + 10, base + 10}] += wS;  // a0xy
            //        gMap[{base + 11, base + 11}] += wS2; // a0yy
            //    }
            //}
            //double meanS0 = 0, meanA00 = 0;
            //for (int imgIdx = 0; imgIdx < numImages; imgIdx++) {
            //    meanS0 += pParams[imgIdx].s0;
            //    meanA00 += pParams[imgIdx].a00;
            //}
            //meanS0 /= numImages;
            //meanA00 /= numImages;

            //double wMean = 0.02;  // 均值约束权重
            //for (int imgIdx = 0; imgIdx < numImages; imgIdx++) {
            //    int base = imgIdx * paramsPerImg;

            //    // s0约束到均值
            //    gMap[{base + 0, base + 0}] += wMean;
            //    gAtWb(base + 0) += wMean * (meanS0 - pParams[imgIdx].s0);

            //    // a00约束到均值  
            //    gMap[{base + 6, base + 6}] += wMean;
            //    gAtWb(base + 6) += wMean * (meanA00 - pParams[imgIdx].a00);
            //}

            // ------ 求解 ------
            std::vector<Eigen::Triplet<double>> trips;
            trips.reserve(gMap.size());
            for (auto& e : gMap) trips.emplace_back(e.first.first, e.first.second, e.second);
            Eigen::SparseMatrix<double> AtWA(n, n);
            AtWA.setFromTriplets(trips.begin(), trips.end());

            Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> ldlt;
            ldlt.compute(AtWA);
            if (ldlt.info() != Eigen::Success) { print2Log("  LDLT failed\n"); return false; }
            Eigen::VectorXd dx = ldlt.solve(gAtWb);
            if (ldlt.info() != Eigen::Success) { print2Log("  LDLT solve failed\n"); return false; }

            // ------ 反缩放更新参数 ------
            for (int imgIdx = 0; imgIdx < numImages; imgIdx++) {
                int base = imgIdx * paramsPerImg;
                SpatialRadParams& p = pParams[imgIdx];
                p.s0 += dx(base + 0);  p.sx += dx(base + 1);  p.sy += dx(base + 2);
                p.sxx += dx(base + 3); p.sxy += dx(base + 4); p.syy += dx(base + 5);
                p.a00 += dx(base + 6);   p.a0x += dx(base + 7);   p.a0y += dx(base + 8);
                p.a0xx += dx(base + 9);  p.a0xy += dx(base + 10); p.a0yy += dx(base + 11);
                p.a10 += dx(base + 12); p.a1x += dx(base + 13); p.a1y += dx(base + 14);
                p.a20 += dx(base + 15); p.a2x += dx(base + 16); p.a2y += dx(base + 17);
            }

            // 打印前3张图参数用于诊断
            int pn = (numImages < 3) ? numImages : 3;
            for (int i = 0; i < pn; i++)
                print2Log("  IMG%d: s0=%.4f a00=%.1f a10=%.4f a20=%.4f\n",
                    i, pParams[i].s0, pParams[i].a00, pParams[i].a10, pParams[i].a20);

            // ------ 并行计算更新后残差，用于 IGG3 权重更新 ------
            std::vector<double> absRes(totalObs, 0.0);
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic,1)
#endif
            for (int fi = 0; fi < numOlpFiles; fi++) {
                const OlpInfo& info = olpList[fi];
                COlpFile olpF; if (!olpF.Load4File(info.path)) continue;
                int oz; OBV* pOs2 = olpF.GetData(&oz);
                int idx = info.idx, idxr = info.idxr;
                int oidBase = fileOidStart[fi];
                for (int vi = 0; vi < oz; vi++, pOs2++) {
                    int oid = oidBase + vi;
                    double cv2 = pOs2->cv[band], rv2 = pOs2->rv[band];
                    double x1 = pOs2->cc, y1 = pOs2->cr;
                    const SpatialRadParams& p1 = pParams[idx];
                    double k1 = getKval(1, pOs2->csz, pOs2->cvz, pOs2->cas);
                    double k2 = getKval(4, pOs2->csz, pOs2->cvz, pOs2->cas);
                    double gb1 = p1.getS(x1, y1) * cv2 - (p1.getA0(x1, y1) + k1 * p1.getA1(x1, y1) + k2 * p1.getA2(x1, y1));
                    double v2;
                    if (idxr == -1) {
                        v2 = gb1 - rv2;
                    }
                    else if (idxr >= 0 && idxr < numImages) {
                        double x2 = pOs2->rc, y2 = pOs2->rr;
                        const SpatialRadParams& p2 = pParams[idxr];
                        double k1r = getKval(1, pOs2->rsz, pOs2->rvz, pOs2->ras);
                        double k2r = getKval(4, pOs2->rsz, pOs2->rvz, pOs2->ras);
                        double gb2 = p2.getS(x2, y2) * rv2 - (p2.getA0(x2, y2) + k1r * p2.getA1(x2, y2) + k2r * p2.getA2(x2, y2));
                        v2 = gb1 - gb2;
                    }
                    else v2 = 0.0;
                    if (oid < (int)absRes.size()) absRes[oid] = fabs(v2);
                }
            }

            if (totalObs > 0) {
                std::vector<double> sorted = absRes;
                std::sort(sorted.begin(), sorted.end());
                double sigma0 = sorted[sorted.size() / 2] * 1.4826;
                if (sigma0 < 30.0) sigma0 = 30.0;

                int outliers = 0;
                for (int oid = 0; oid < totalObs; oid++) {
                    double std_res = absRes[oid] / sigma0;
                    double iw = initWeights[oid];  // ★ 以质量权重为上限
                    double new_w = IGG3Weight(std_res, iw);
                    if (new_w < obsWeights[oid] * 0.8) outliers++;
                    obsWeights[oid] = new_w;
                }
                print2Log("  sigma0=%.2f  outliers=%d\n", sigma0, outliers);
            }
            rmse = 0.0; int nrmse = 0;
            for (int i = 0; i < totalObs; i++) {
                if (absRes[i] > 0.0) { rmse += absRes[i] * absRes[i]; nrmse++; }
            }
            if (nrmse > 0) rmse = sqrt(rmse / nrmse);
            //rmse = (validObs > 0) ? sqrt(sumSqRes / validObs) : 0.0;
            return true;
    }

// ---------- solveBand ----------
bool SpatialRadBA::solveBand(
    int numImages, int band,
    const char* const* tskFiles,
    const char* strRom,
    SpatialRadParams* pParams,
    void (*progressCallback)(const char*),
    const std::vector<const char*>& excellentFiles,
    const std::vector<const char*>& goodFiles,
    const std::vector<const char*>& fairFiles,
    const std::vector<const char*>& poorFiles)
{
    if (!initializeGlobal(numImages, pParams)) return false;
    if (!m_useSpatial) return true;

    // ★ 关键修复：在此处构建 olpList（含质量权重），并缓存到成员变量，
    //   供所有迭代的 solveIteration 共用，避免重复扫描文件且保证权重一致。
    CollectOlpListWithQuality(numImages, tskFiles,
        excellentFiles, goodFiles, fairFiles, poorFiles,
        m_cachedOlpList);

    int numOlpFiles = (int)m_cachedOlpList.size();

    // 预计算各文件在 obsWeights 中的起始偏移，并统计总观测数
    m_cachedFileOidStart.resize(numOlpFiles + 1, 0);
    int totalObs = 0;
    for (int fi = 0; fi < numOlpFiles; fi++) {
        m_cachedFileOidStart[fi] = totalObs;
        COlpFile olpF;
        if (olpF.Load4File(m_cachedOlpList[fi].path)) {
            int oz; olpF.GetData(&oz); totalObs += oz;
        }
        m_cachedFileOidStart[fi + 1] = totalObs;
    }

    if (totalObs == 0) {
        print2Log("  ERROR: No observations found in quality OLP files!\n");
        return false;
    }

    // ★ 初始化 obsWeights / initWeights（以质量等级权重为各点上限）
    std::vector<double> obsWeights(totalObs);
    std::vector<double> initWeights(totalObs);
    {
        int oid = 0;
        for (int fi = 0; fi < numOlpFiles; fi++) {
            COlpFile olpF;
            if (!olpF.Load4File(m_cachedOlpList[fi].path)) continue;
            int oz; olpF.GetData(&oz);
            double w0 = m_cachedOlpList[fi].initWeight;  // ★ 质量权重
            for (int vi = 0; vi < oz; vi++, oid++)
                obsWeights[oid] = initWeights[oid] = w0;
        }
    }

    // 统计各质量等级观测数量，打印到日志
    {
        int nEx = 0, nGd = 0, nFr = 0, nPr = 0, nDef = 0;
        for (int fi = 0; fi < numOlpFiles; fi++) {
            COlpFile olpF; if (!olpF.Load4File(m_cachedOlpList[fi].path)) continue;
            int oz; olpF.GetData(&oz);
            double w = m_cachedOlpList[fi].initWeight;
            // 以 idxr 决定绝对/相对后去掉0.4系数再比较
            double qw = (m_cachedOlpList[fi].idxr == -1) ? w : w / 0.5;
            if (qw >= 0.95) nEx += oz;
            else if (qw >= 0.75) nGd += oz;
            else if (qw >= 0.35) nFr += oz;
            else                 nPr += oz;
        }
        print2Log("  Effective obs by quality: Excellent=%d, Good=%d, Fair=%d, Poor=%d (Total=%d)\n",
            nEx, nGd, nFr, nPr, totalObs);
    }

    m_iterations = 0;
    double prevRMSE = 1e10;
    for (int iter = 0; iter < m_maxIter; iter++) {
        print2Log("  iter %d ... ", iter + 1); fflush(stdout);
        double rmse = 0.0;
        if (!solveIteration(numImages, band, tskFiles, strRom,
            pParams, obsWeights, initWeights, rmse))
            return false;

        m_iterations = iter + 1;
        m_finalRMSE = rmse;
        print2Log(" RMSE=%.4f\n", rmse); fflush(stdout);

        if (iter > 0) {
            double improvement = (prevRMSE - rmse) / prevRMSE;
            if (improvement < 0.01) {
                print2Log("  Converged (improvement < 1%%)\n");
                break;
            }
        }
        prevRMSE = rmse;
    }
    return true;
}