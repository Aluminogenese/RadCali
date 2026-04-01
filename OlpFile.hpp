/*----------------------------------------------------------------------+
|     OlpFile                                                           |
|       Author:     DuanYanSong  2025/06/27                             |
|            Ver 1.0                                                    |
|       Copyright (c)2025, WHU RSGIS DPGrid Group                       |
|            All rights reserved.                                       |
|       ysduan@whu.edu.cn; ysduan@sohu.com                              |
+----------------------------------------------------------------------*/
#ifndef OLPFILE_H_DUANYANSONG_2025_06_27_10_41_849758923475435
#define OLPFILE_H_DUANYANSONG_2025_06_27_10_41_849758923475435

#include "math.h"
#include "stdio.h"
#include "stdlib.h"

#ifdef WIN32
#include "conio.h"
#else
#include "Linux.h"
#endif

typedef struct tagOBV{
    int cc,cr,rc,rr;
    float cv[4],rv[4];
    float csz,cvz,cas,rsz,rvz,ras;
}OBV;

class COlpFile{
public:
    COlpFile(){ m_obs.Reset(NULL,1024*512); m_obs.SetSize(0); };
    ~COlpFile(){ m_obs.Reset(); };
    
    int  GetSize(){ return m_obs.GetSize(); }
    void SetSize(int sz){ m_obs.SetSize(sz); }    
    OBV* GetData(int *sz){ return m_obs.GetData(*sz); }   
    int  Append( OBV obv ){ return  m_obs.Append(obv); }
    int  Append( double sz ,double  vz,double  as,int cc,int cr,const WORD* cv,
                 double sz1,double vz1,double as1,int rc,int rr,const WORD* rv ){
        OBV obv={ cc,cr,rc,rr,(float)cv[0],(float)cv[1],(float)cv[2],(float)cv[3],
            (float)rv[0],(float)rv[1],(float)rv[2],(float)rv[3],sz,vz,as,sz1,vz1,as1 };
        return Append(obv);
    };

    bool Load4File(LPCSTR lpstrPN){
        int i,oz; OBV* pOs; DWORD rw;
        HANDLE hFile = ::CreateFile( lpstrPN,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL );
        if ( hFile==INVALID_HANDLE_VALUE ) return false;
        memset( &m_olpHdr,0,sizeof(m_olpHdr) ); 
        ::ReadFile( hFile,&m_olpHdr,sizeof(m_olpHdr),&rw,NULL );
        if ( strcmp(m_olpHdr.strTag,"OLP_V1")==0 ){
            m_obs.SetSize(m_olpHdr.ptSum);
            pOs = m_obs.GetData(m_olpHdr.ptSum);
            ::ReadFile( hFile,pOs,m_olpHdr.ptSum*sizeof(OBV),&rw,NULL );
            ::CloseHandle( hFile );
        }else{
            ::CloseHandle(hFile);

            FILE *fOlp = fopen( lpstrPN,"rt" ); if ( !fOlp ) return false;
            fscanf( fOlp,"%d",&oz );
            m_obs.SetSize(oz);
            pOs = m_obs.GetData(oz);
            for( i=0;i<oz;i++,pOs++ ){
                fscanf( fOlp,"%f%f%f%d%d%f%f%f%f%f%f%f%d%d%f%f%f%f",
                    &pOs->csz,&pOs->cvz,&pOs->cas,&pOs->cc,&pOs->cr,&pOs->cv[0],&pOs->cv[1],&pOs->cv[2],&pOs->cv[3],
                    &pOs->rsz,&pOs->rvz,&pOs->ras,&pOs->rc,&pOs->rr,&pOs->rv[0],&pOs->rv[1],&pOs->rv[2],&pOs->rv[3] );
            }
            fclose(fOlp);
        }
        return true;
    };
    bool Save2File(LPCSTR lpstrPN,BOOL bBin=TRUE){
        int i,oz=0; OBV*  pOs = m_obs.GetData(oz); if ( oz<1 ) return false;
        if ( bBin ){
            HANDLE hFile = ::CreateFile( lpstrPN,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,0,NULL );
            if ( hFile==INVALID_HANDLE_VALUE ) return false;
            memset( &m_olpHdr,0,sizeof(m_olpHdr) ); DWORD rw;
            strcpy( m_olpHdr.strTag,"OLP_V1" ); m_olpHdr.ptSum = oz;
            ::WriteFile( hFile,&m_olpHdr,sizeof(m_olpHdr),&rw,NULL );
            ::WriteFile( hFile,pOs,m_olpHdr.ptSum*sizeof(OBV),&rw,NULL );
            ::CloseHandle( hFile );
        }else{
            FILE *fOlp = fopen( lpstrPN,"wt" ); if ( !fOlp ) return false;
            fprintf( fOlp,"%d\n",oz );
            for( i=0;i<oz;i++,pOs++ ){
                fprintf( fOlp,"\t%8.5f %8.5f %8.5f %6d %6d %9.1f %9.1f %9.1f %9.1lf %8.5f %8.5f %8.5f %6d %6d %9.1f %9.1f %9.1f %9.1f\n",
                    pOs->csz,pOs->cvz,pOs->cas,pOs->cc,pOs->cr,pOs->cv[0],pOs->cv[1],pOs->cv[2],pOs->cv[3],
                    pOs->rsz,pOs->rvz,pOs->ras,pOs->rc,pOs->rr,pOs->rv[0],pOs->rv[1],pOs->rv[2],pOs->rv[3] );
            }
            fclose(fOlp);
        }
        return true;
    };    
    class CArray_OBV
    {
    public:
        CArray_OBV( OBV* pBuf=NULL,int size=0 ){    m_maxSize = m_size = size; m_pBuf = NULL;if ( m_size ) m_pBuf = new OBV[m_size]; if ( pBuf    ) memcpy( m_pBuf,pBuf,sizeof(OBV)*m_size ); };
        virtual ~CArray_OBV(){ if (m_pBuf) delete []m_pBuf; };
        inline  OBV*    GetData(int &size){ size=m_size; return m_pBuf; };
        inline  void    SetSize(int size ){ if (size<m_maxSize) m_size=size; else Reset(NULL,size);  };
        inline  int     Append( OBV uint ){ if ( m_size >= m_maxSize ){  m_maxSize += 1024; OBV* pOld = m_pBuf; m_pBuf  = new OBV[m_maxSize]; memset( m_pBuf,0,sizeof(OBV)*m_maxSize ); memcpy( m_pBuf,pOld,sizeof(OBV)*m_size );    delete []pOld; } m_pBuf[m_size]=uint;m_size++;return (m_size-1); };
        inline  void    Reset( OBV* pBuf=NULL,int size=0 ){ if (m_pBuf) delete []m_pBuf; m_pBuf = NULL; m_maxSize = m_size = size; if ( m_maxSize ){ m_pBuf = new OBV[m_maxSize +8]; memset( m_pBuf,0,sizeof(OBV)*m_maxSize ); } if ( pBuf ) memcpy( m_pBuf,pBuf,sizeof(OBV)*m_size ); }
        inline  OBV&    operator[](int idx){ return m_pBuf[idx];   };
        inline  int     GetSize(){ return m_size;                  };
    private:
        OBV*    m_pBuf;
        int     m_size;
        int     m_maxSize;
    }m_obs; 
    struct OLPHDR{
        char strTag[8];
        int ptSum;
    }m_olpHdr;

    // ── 新增：获取带质量权重的观测值数组 ──
    struct ObsWithWeight {
        OBV* pObs;           // 观测值
        double* pWeights;    // 初始权重
        int count;
    };

    // 从多个质量文件合并加载，返回带权重的观测集合
    ObsWithWeight LoadMergedWithWeights(
        const char* strExcellent,   // QUALITY_EXCELLENT
        const char* strGood,        // QUALITY_GOOD
        const char* strFair,        // QUALITY_FAIR
        const char* strPoor)        // QUALITY_POOR
    {
        ObsWithWeight result = { NULL, NULL, 0 };

        // 加载各质量等级的文件
        COlpFile olpEx, olpGd, olpFr, olpPr;
        int nEx = 0, nGd = 0, nFr = 0, nPr = 0;

        if (strExcellent && strExcellent[0]) {
            olpEx.Load4File(strExcellent);
            nEx = olpEx.GetSize();
        }
        if (strGood && strGood[0]) {
            olpGd.Load4File(strGood);
            nGd = olpGd.GetSize();
        }
        if (strFair && strFair[0]) {
            olpFr.Load4File(strFair);
            nFr = olpFr.GetSize();
        }
        if (strPoor && strPoor[0]) {
            olpPr.Load4File(strPoor);
            nPr = olpPr.GetSize();
        }

        int totalN = nEx + nGd + nFr + nPr;
        if (totalN <= 0) return result;

        // 分配内存
        result.pObs = new OBV[totalN];
        result.pWeights = new double[totalN];
        result.count = totalN;

        int idx = 0;
        OBV* p;
        int dummy = 0;

        // 1.0 权重：优秀点（最可靠）
        if (nEx > 0) {
            p = olpEx.GetData(&dummy);
            memcpy(result.pObs + idx, p, nEx * sizeof(OBV));
            std::fill(result.pWeights + idx, result.pWeights + idx + nEx, 1.0);
            idx += nEx;
        }

        // 0.8 权重：良好点
        if (nGd > 0) {
            p = olpGd.GetData(&dummy);
            memcpy(result.pObs + idx, p, nGd * sizeof(OBV));
            std::fill(result.pWeights + idx, result.pWeights + idx + nGd, 0.6);
            idx += nGd;
        }

        // 0.5 权重：一般点（可选，视情况包含）
        if (nFr > 0) {
            p = olpFr.GetData(&dummy);
            memcpy(result.pObs + idx, p, nFr * sizeof(OBV));
            std::fill(result.pWeights + idx, result.pWeights + idx + nFr, 0.25);
            idx += nFr;
        }

        // 0.2 权重：较差点（可选，低权重参与或完全排除）
        if (nPr > 0) {
            p = olpPr.GetData(&dummy);
            memcpy(result.pObs + idx, p, nPr * sizeof(OBV));
            std::fill(result.pWeights + idx, result.pWeights + idx + nPr, 0.05);
            idx += nPr;
        }

        return result;
    }

    // 清理合并结果
    static void FreeMergedWeights(ObsWithWeight& mw) {
        if (mw.pObs) { delete[] mw.pObs; mw.pObs = NULL; }
        if (mw.pWeights) { delete[] mw.pWeights; mw.pWeights = NULL; }
        mw.count = 0;
    }
};

#endif