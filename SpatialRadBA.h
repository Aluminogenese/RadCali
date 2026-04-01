#pragma once
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <vector>
#include <string>

// ==================== 空间变化参数（单个波段）====================
class SpatialRadParams {
public:
    double s0, sx, sy, sxx, sxy, syy;
    double a00, a0x, a0y, a0xx, a0xy, a0yy;
    double a10, a1x, a1y;
    double a20, a2x, a2y;
    int imgWidth, imgHeight;

    SpatialRadParams();
    inline double normX(double x) const { return (x - imgWidth * 0.5) / (imgWidth * 0.5); }
    inline double normY(double y) const { return (y - imgHeight * 0.5) / (imgHeight * 0.5); }
    inline double getS(double x, double y) const {
        double xn = normX(x), yn = normY(y);
        return s0 + sx * xn + sy * yn + sxx * xn * xn + sxy * xn * yn + syy * yn * yn;
    }
    inline double getA0(double x, double y) const {
        double xn = normX(x), yn = normY(y);
        return a00 + a0x * xn + a0y * yn + a0xx * xn * xn + a0xy * xn * yn + a0yy * yn * yn;
    }
    inline double getA1(double x, double y) const {
        double xn = normX(x), yn = normY(y);
        return a10 + a1x * xn + a1y * yn;
    }
    inline double getA2(double x, double y) const {
        double xn = normX(x), yn = normY(y);
        return a20 + a2x * xn + a2y * yn;
    }
    void initFromGlobal(double s, double a0, double a1, double a2);
    void getGlobalParams(double& s, double& a0, double& a1, double& a2) const {
        s = s0; a0 = a00; a1 = a10; a2 = a20;
    }
};

// ==================== 空间变化辐射平差求解器 ====================
class SpatialRadBA {
public:
    SpatialRadBA();
    ~SpatialRadBA();

    void setUseSpatialVarying(bool use) { m_useSpatial = use; }
    void setSmoothWeight(double w) { m_smoothWeight = w; }
    void setMaxIterations(int n) { m_maxIter = n; }
    void setAvgKernel(const double* avK1, const double* avK2, int numImages) {
        m_avK1.assign(avK1, avK1 + numImages);
        m_avK2.assign(avK2, avK2 + numImages);
    }
    struct OlpInfo {
        char   path[512];
        int    idx;
        int    idxr;
        double initWeight;  // Excellent=1.0, Good=0.8, Fair=0.5, Poor=0.2
        // 绝对约束(idxr==-1)额外乘以0.4
    };
    bool solveBand(
        int numImages,
        int band,
        const char* const* tskFiles,
        const char* strRom,
        SpatialRadParams* pParams,
        void (*progressCallback)(const char*) = NULL,
        const std::vector<const char*>& excellentFiles = {},
        const std::vector<const char*>& goodFiles = {},
        const std::vector<const char*>& fairFiles = {},
        const std::vector<const char*>& poorFiles = {}
    );

    double getFinalRMSE()  const { return m_finalRMSE; }
    int    getIterations() const { return m_iterations; }

private:
    bool   m_useSpatial;
    double m_smoothWeight;
    int    m_maxIter;
    double m_finalRMSE;
    int    m_iterations;
    std::vector<double> m_avK1;
    std::vector<double> m_avK2;

    // 迭代间共享的 OLP 文件列表缓存（在 solveBand 中建立）
    // 包含质量等级对应的初始权重，所有 solveIteration 调用共用
    
    std::vector<OlpInfo> m_cachedOlpList;
    std::vector<int>     m_cachedFileOidStart;

    bool initializeGlobal(int numImages, SpatialRadParams* pParams);

    bool solveIteration(int numImages, int band, const char* const* tskFiles,
        const char* strRom, SpatialRadParams* pParams,
        std::vector<double>& obsWeights,
        std::vector<double>& initWeights,
        double& rmse);
};