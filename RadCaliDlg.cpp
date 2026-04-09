// RadCaliDlg.cpp : implementation file
//
/*----------------------------------------------------------------------+
|		RadCaliDlg														|
|       Author:     DuanYanSong  2025/06/08				                |
|            Ver 1.0													|
|       Copyright (c)2025, WHU RSGIS DPGrid Group                       |
|	         All rights reserved.                                       |
|		ysduan@whu.edu.cn; ysduan@sohu.com              				|
+----------------------------------------------------------------------*/
// ======================================================================
// GDAL 与 MFC (stdafx.h) 共存方案：
//
// 方案A（推荐）：在项目属性 → C/C++ → 预编译头 中，
//   把本文件单独设为"不使用预编译头"，然后按下面顺序 include。
//
// 方案B：在 stdafx.h 末尾加入 GDAL 头文件（让预编译头包含 GDAL）：
//   #include "gdal_priv.h"
//   #include "ogr_spatialref.h"
//   #include "ogr_api.h"
//   此时本文件保持 stdafx.h 在最前即可，删除下面的 GDAL include。
//
// 根本原因：MFC stdafx.h 会 #define min/max 等破坏 GDAL 模板的宏；
// GDAL 头文件必须在这些宏定义之前被解析。
// ======================================================================

// GDAL 先于 stdafx.h 包含（方案A）
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <ogr_api.h>

// 此文件若启用预编译头，请在项目属性将其设为"不使用预编译头"
// （右键 .cpp → 属性 → C/C++ → 预编译头 → 不使用预编译头）
#include "stdafx.h"
#include "conio.h"
#include "RadCaliDlg.h"

#include "WaitRunExe.hpp"
#include "WuAboutDlg.hpp"

#include "WuLog.hpp"

#include "WuMath.hpp"
#include "PositionSolar.hpp"
#include "WuGeoCvt.h"
#include "TMGeom.hpp"
#include "TMFile.hpp"
#include "OlpFile.hpp"

//#include <opencv2/opencv.hpp>
//#include <opencv2/features2d.hpp>
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <vector>
#include <algorithm>

using Eigen::VectorXd;
using Eigen::MatrixXd;

#pragma comment(lib,"opencv_imgproc490d.lib")
#pragma comment(lib,"gdal_i.lib")
#define USE_MY_MATCH
#ifndef RAD_PATCH_RADIUS
#define RAD_PATCH_RADIUS   3    // 补丁半径 => 9x9 patch
#endif
#ifndef RAD_MIN_STD
#define RAD_MIN_STD        1.0  // 最小标准差（灰度）
#endif
#ifndef RAD_MIN_ZNCC
#define RAD_MIN_ZNCC       0.7 // 最小 ZNCC 阈值
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define TSK_STA_READY   "等待执行"
#define TSK_STA_SEND    "提交任务..."
#define TSK_STA_RUN     "任务执行中..."
#define TSK_STA_OVER    "任务完成"
#define TSK_STA_EXIT    "程序退出"
#define TSK_STA_TERM    "执行被取消"

#define MAX_CPU             64
static HWND  gs_hWnd;
static ULONG gs_hThreadId[MAX_CPU];
static ULONG gs_hProcId[MAX_CPU];
static char  gs_strCmd[MAX_CPU][1024];
DWORD WINAPI CupThread_CRadCaliDlg( LPVOID lpParam ) // 线程入口函数
{
    ULONG id = GetCurrentThreadId(); int i,tskId=0; char str[512];
    for ( i=0;i<MAX_CPU;i++ ){ if ( gs_hThreadId[i]==id ) break; }
    if ( i==MAX_CPU ){ cprintf("\n\nError. gs_hThreadId[i]!=id \n\n"); return FALSE; }
    
    cprintf("Exec: %s \n",lpParam ); sscanf( (char*)lpParam,"%s %d",str,&tskId );  
    
    WaitRunExe( (char*)lpParam,SW_HIDE,(UINT*)(gs_hProcId+i) );
    gs_hProcId[i]=0; gs_hThreadId[i] = 0; 
    if ( IsWindow(gs_hWnd) ) ::PostMessage(gs_hWnd,WM_OUTPUT_MSG,TASKOVER,tskId );  
    ::ExitThread( 0 );  return TRUE;
}


#ifndef _ISEXIST
#define _ISEXIST
inline BOOL IsExist(LPCSTR lpstrPathName){
    WIN32_FIND_DATA fd; HANDLE hFind=INVALID_HANDLE_VALUE;
    hFind = ::FindFirstFile(lpstrPathName,&fd);
    if ( hFind==INVALID_HANDLE_VALUE ) return FALSE;
    ::FindClose(hFind); return TRUE;
}
#endif

#ifndef _CreateDir
#define _CreateDir
static BOOL CreateDir(LPCTSTR szPath){
    WIN32_FIND_DATA fd; HANDLE hFind = ::FindFirstFile(szPath,&fd);
    if ( hFind!=INVALID_HANDLE_VALUE ){ ::FindClose(hFind); ::CreateDirectory(szPath,NULL); return TRUE; }
    char strPath[512]; strcpy( strPath,szPath );
    char *pSplit1 = strrchr( strPath,'\\' ); 
    char *pSplit2 = strrchr( strPath,'/' );
    char *pSplit = pSplit1>pSplit2?pSplit1:pSplit2;
    if ( !pSplit ) return TRUE; else *pSplit = 0; 
    if ( !CreateDir(strPath) ) return FALSE;
    return ::CreateDirectory(szPath,NULL);
}
#endif

BOOL CtrlHandler(DWORD fdwCtrlType) 
{ 
    switch (fdwCtrlType) 
    { 
        case CTRL_BREAK_EVENT:
        case CTRL_C_EVENT:
        case CTRL_LOGOFF_EVENT: 
        case CTRL_SHUTDOWN_EVENT: 
        case CTRL_CLOSE_EVENT:
            AfxGetMainWnd()->PostMessage(WM_QUIT);
            return TRUE;         
        default: 
            return FALSE; 
    } 
} 

BOOL MchTie(LPCSTR lpstrPar);


class CRadCaliApp : public CWinApp
{
public:
    CRadCaliApp(){};
    virtual BOOL InitInstance(){ 
    // 控制台输出
        char szBuf[256]; ::GetModuleFileName(NULL,szBuf,sizeof(szBuf));
        strcpy( strrchr(szBuf,'\\'),"\\debug.flag" ); 
        if ( IsExist(szBuf) ){ AllocConsole(); SetConsoleCtrlHandler( (PHANDLER_ROUTINE)CtrlHandler,TRUE ); }

        if (strlen(m_lpCmdLine) > 10 && strchr(m_lpCmdLine, '@')) {
            char strCmd[1024]; strcpy(strCmd, m_lpCmdLine); m_lpCmdLine[0] = 0;
            if (strstr(strCmd, "TIE@")) { return MchTie(strchr(strCmd, '@') + 1); }        
        }

        SetRegistryKey(_T("WHU RSGIS DPGrid Group"));

		CRadCaliDlg dlg; m_pMainWnd=&dlg; dlg.DoModal(); 
		return FALSE; 
	};
}theApp;


static inline char* itostr(int num )
{
    static char str[32];
    char *p = str;  p[8] = 0; char *Hex = "0123456789ABCDEFF";
    p[0] = Hex[num     & 0xF]; p[1] = Hex[num>>4  & 0xF];
    p[2] = Hex[num>>8  & 0xF]; p[3] = Hex[num>>12 & 0xF]; 
    p[4] = Hex[num>>16 & 0xF]; p[5] = Hex[num>>20 & 0xF]; 
    p[6] = Hex[num>>24 & 0xF]; p[7] = Hex[num>>28 & 0xF];
	return str;
};

/////////////////////////////////////////////////////////////////////////////
// CRadCaliDlg dialog
CRadCaliDlg::CRadCaliDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CRadCaliDlg::IDD, pParent)
{
    m_utmZn = 50;
	//{{AFX_DATA_INIT(CRadCaliDlg)
	m_strMxCore = _T("8");
	m_strRet = _T("");
	m_strBas = _T("");
    m_strLasDir = _T("");
	m_gs = 11;
	m_ws = 3;
	//m_bTie = TRUE;
    m_bTie = FALSE;
	m_bAdj = TRUE;
	m_bTxt = FALSE;
	//}}AFX_DATA_INIT
	// Note that LoadIcon does not require a subsequent DestroyIcon in Win32
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);

    m_hThread = NULL;
    m_hEThEHdl = m_hW4EndHdl = NULL;
    memset( gs_hThreadId,0,sizeof(gs_hThreadId) );
    memset( gs_hProcId,0,sizeof(gs_hProcId) );
}

void CRadCaliDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CRadCaliDlg)
	DDX_Control(pDX, IDC_COMBO_CORE, m_cmbMxCore);
	DDX_CBString(pDX, IDC_COMBO_CORE, m_strMxCore);
	DDX_Control(pDX, IDC_EDIT_REF, m_edtBas);
	DDX_Control(pDX, IDC_PROGRESS_CUR, m_progCur);
	DDX_Control(pDX, IDC_PROGRESS_ALL, m_progAll);
	DDX_Control(pDX, IDC_LIST_CTRL, m_listCtrl);
    DDX_Control(pDX, IDC_EDIT_RET, m_editRet);
    DDX_Text(pDX, IDC_EDIT_RET, m_strRet);
	DDX_Text(pDX, IDC_EDIT_REF, m_strBas);
	DDX_Text(pDX, IDC_EDIT_GS, m_gs);
	DDV_MinMaxInt(pDX, m_gs, 1, 999);
	DDX_Text(pDX, IDC_EDIT_WS, m_ws);
	DDV_MinMaxInt(pDX, m_ws, 3, 11);
	DDX_Check(pDX, IDC_CHECK_MCH, m_bTie);
	DDX_Check(pDX, IDC_CHECK_ADJ, m_bAdj);
	DDX_Check(pDX, IDC_CHECK_TXT, m_bTxt);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CRadCaliDlg, CDialog)
    ON_MESSAGE( WM_OUTPUT_MSG,OnOutputMsg   )

	//{{AFX_MSG_MAP(CRadCaliDlg)
	ON_WM_SYSCOMMAND()
    ON_WM_INITMENUPOPUP()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_SIZE()
    ON_WM_DROPFILES()
	ON_WM_DESTROY()
    ON_BN_CLICKED(IDC_BUTTON_RET, OnButtonRet)
    ON_NOTIFY(LVN_KEYDOWN,IDC_LIST_CTRL, OnKeydownListCtrl)
	ON_BN_CLICKED(IDC_BUTTON_LOAD, OnButtonLoad)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CRadCaliDlg message handlers
LRESULT CRadCaliDlg::OnOutputMsg(WPARAM wParam, LPARAM lParam)
{
	switch( wParam )
	{
        case PROC_MSG:
			GetDlgItem( IDC_STATIC_MSG )->SetWindowText( LPCSTR(lParam) );
            UpdateWindow();
        	break;
		case PROC_START:
			m_progAll.ShowWindow(SW_SHOW);
			m_progAll.SetPos(0);
			m_progAll.SetStep(1);
			m_progAll.SetRange( 0,int(lParam) );
			BeginWaitCursor();
			break;
		case PROC_STEP:
			BeginWaitCursor();
			m_progAll.StepIt();
			if ( GetAsyncKeyState(VK_ESCAPE) && lParam ) *((UINT*)lParam) = TRUE;
            UpdateWindow();
			break;
		case PROC_OVER:
			m_progAll.ShowWindow(SW_HIDE);
			EndWaitCursor();

		case PROC_FSCR:
			break;

        case PROC_STARTSUB:
			BeginWaitCursor();
			m_progCur.ShowWindow(SW_SHOW);
			m_progCur.SetPos(0);
			m_progCur.SetStep(1);
			m_progCur.SetRange( 0,int(lParam) );
			break;
		case PROC_STEPSUB:
			BeginWaitCursor();
			m_progCur.StepIt();
            UpdateWindow();
			break;
		case PROC_OVERSUB:
			m_progCur.ShowWindow(SW_HIDE);
			break;
		case UPDATEDATA:
			UpdateData( lParam );
			break;
        case THREADEND:
            GetDlgItem(IDOK)->EnableWindow( m_hThread==NULL );
            CDialog::OnOK();
            break;
        case TASKOVER:
            OnTaskOver( lParam );
            break;
		default:
			break;
	}
    return 0;
}


void CRadCaliDlg::OnDestroy() 
{
	CDialog::OnDestroy();
	

    for ( int i=0;i<MAX_CPU;i++ ){
        if ( gs_hProcId[i] ){
            HANDLE hProc = ::OpenProcess( PROCESS_TERMINATE,FALSE,gs_hProcId[i] );
            if ( hProc ) ::TerminateProcess( hProc,0x22 );
            gs_hProcId[i] = 0; Sleep(8);
        }
    }
	if ( m_hEThEHdl )
	{
		m_hW4EndHdl = ::CreateEvent( NULL,TRUE,TRUE,itostr( GetTickCount() ) );
		::ResetEvent( m_hW4EndHdl ); ::SetEvent( m_hEThEHdl ); 
		if ( m_hThread ) ::WaitForSingleObject( m_hW4EndHdl,2048 );
		if ( m_hW4EndHdl ) ::CloseHandle(m_hW4EndHdl);  m_hW4EndHdl = NULL;
		
		if (m_hEThEHdl) ::CloseHandle(m_hEThEHdl); m_hEThEHdl = NULL;
	}
	if (m_hThread){ ::TerminateThread(m_hThread,0); ::CloseHandle(m_hThread); m_hThread = NULL; }
}

enum TSK_ITEM{
    TSK_ID      = 0,
    TSK_GRP     = 1,
    TSK_STA     = 2,
    TSK_CMD     = 3,
    TSK_PAR     = 4,
};

static void MyGetSystemInfo(SYSTEM_INFO *si){
    typedef FARPROC(WINAPI * PFNGetNativeSystemInfo)(IN SYSTEM_INFO*);
    PFNGetNativeSystemInfo pGNSI = (PFNGetNativeSystemInfo)GetProcAddress( GetModuleHandle(TEXT("kernel32.dll")),"GetNativeSystemInfo" );
    if ( pGNSI ) pGNSI( si ); else GetSystemInfo( si );
}

BOOL CRadCaliDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	// Add "About..." menu item to system menu.

	// IDM_ABOUTBOX must be in the system command range.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);
	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL)
	{
		CString strAboutMenu;
		strAboutMenu.LoadString(IDS_ABOUTBOX);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon
	
	// TODO: Add extra initialization here    
	m_listCtrl.InsertColumn( 0, _T( "Source" ), LVCFMT_LEFT);		
	m_listCtrl.InsertColumn( 1, _T("Xs Ys Zs grdZ yy mm dd ho mi se"), LVCFMT_LEFT);

    CRect rect; m_listCtrl.GetClientRect(&rect);
	m_listCtrl.SetColumnWidth(0, rect.Width()/5*4 );
	m_listCtrl.SetColumnWidth(1, 256 );
	ListView_SetExtendedListViewStyle( m_listCtrl.m_hWnd, LVS_EX_GRIDLINES|LVS_EX_FULLROWSELECT|LVS_EX_INFOTIP );

    m_strRet = AfxGetApp()->GetProfileString( "CRadCaliDlg","Prj","" );
    m_strLasDir = AfxGetApp()->GetProfileString("CRadCaliDlg", "LasDir", "");

    SYSTEM_INFO sysInfo; MyGetSystemInfo (&sysInfo);   
    m_strMxCore.Format( "%d",sysInfo.dwNumberOfProcessors-1 );
    m_cmbMxCore.AddString( m_strMxCore );
    m_strMxCore = AfxGetApp()->GetProfileString( "CRadCaliDlg","CPUs",m_strMxCore );

    m_ompThreads = (std::max)(1, atoi(m_strMxCore));
#ifdef _OPENMP
    omp_set_num_threads(m_ompThreads);
#endif
    UpdateData(FALSE);
	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CRadCaliDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CWuAboutDlg dlg( "RadCali" ); dlg.DoModal();
		//class CAboutDlg : public CDialog
		//{
		//public:
		//    CAboutDlg(): CDialog(CAboutDlg::IDD){};
		//    enum { IDD = IDD_ABOUTBOX };
		//}dlg; dlg.DoModal();	

	}else
	{
		CDialog::OnSysCommand(nID, lParam);
	}
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.
void CRadCaliDlg::OnPaint() 
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, (WPARAM) dc.GetSafeHdc(), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialog::OnPaint();
	}
}

void CRadCaliDlg::OnInitMenuPopup(CMenu* pMenu, UINT nIndex, BOOL bSysMenu) 
{
	ASSERT(pMenu != NULL);
	// check the enabled state of various menu items

    if ( bSysMenu )
    {
        
    }else
    {
        CCmdUI state; state.m_pMenu = pMenu;
	    ASSERT(state.m_pOther == NULL);
	    ASSERT(state.m_pParentMenu == NULL);
        state.m_nIndexMax = pMenu->GetMenuItemCount();
	    for ( state.m_nIndex=0; state.m_nIndex<state.m_nIndexMax;state.m_nIndex++ )
	    {
		    state.m_nID = pMenu->GetMenuItemID(state.m_nIndex);
		    if (state.m_nID == 0) continue; 

		    ASSERT(state.m_pOther == NULL);
		    ASSERT(state.m_pMenu != NULL);

		    if (state.m_nID == (UINT)-1)
		    {
			    // possibly a popup menu, route to first item of that popup
			    state.m_pSubMenu = pMenu->GetSubMenu(state.m_nIndex);
			    if (state.m_pSubMenu == NULL ||(state.m_nID = state.m_pSubMenu->GetMenuItemID(0)) == 0 ||state.m_nID == (UINT)-1) continue;      // first item of popup can't be routed to
			    state.DoUpdate(this, FALSE );    // popups are never auto disabled
		    }
		    else
		    {
			    state.m_pSubMenu = NULL;
			    state.DoUpdate( this,FALSE );
		    }
		    // adjust for menu deletions and additions
		    UINT nCount = pMenu->GetMenuItemCount();
		    if (nCount < state.m_nIndexMax)
		    {
			    state.m_nIndex -= (state.m_nIndexMax - nCount);
			    while (state.m_nIndex<nCount && pMenu->GetMenuItemID(state.m_nIndex) == state.m_nID)
                { state.m_nIndex++; }
		    }
		    state.m_nIndexMax = nCount;
	    }
    }
}

void CRadCaliDlg::OnSize(UINT nType, int cx, int cy) 
{
	CDialog::OnSize(nType, cx, cy);	
	// TODO: Add your message handler code here	
}


static CStringArray fileList;
static void Recurse(LPCTSTR pstr)
{
    CFileFind   finder;
    CString strLine,strWildcard(pstr);  strWildcard += _T("\\*.*");
    // start working for files
    BOOL bWorking = finder.FindFile(strWildcard);
    while (bWorking)
    {
        bWorking = finder.FindNextFile();
        
        if (finder.IsDots()) continue;
        
        if (finder.IsDirectory())  Recurse( finder.GetFilePath() );
        else
        {
            strLine = finder.GetFilePath();
            if ( _strcmpi( strLine.Right(4),".txt" )==0 )
                 fileList.Add( LPCSTR(strLine) );
        }       
    }
    finder.Close();
}

void CRadCaliDlg::OnDropFiles(HDROP hDropInfo) 
{
    /*
    CWaitCursor wait; char fName[ FILENAME_MAX ];
    CString strLine; WIN32_FIND_DATA ffD; int item;
    int wNumFilesDropped = ::DragQueryFile(hDropInfo, -1, NULL, 0);
	for( int idx=0,i=0;i<wNumFilesDropped;i++ )
	{	
        ::DragQueryFile( hDropInfo, i,fName,FILENAME_MAX );	
        
        strLine = fName; memset( &ffD,0,sizeof(ffD) );
		::FindClose( ::FindFirstFile( (LPTSTR)fName,&ffD) );
        if ( (ffD.dwFileAttributes&CFile::directory)==CFile::directory )
        {
            fileList.RemoveAll();
            Recurse(strLine);
            for ( int l=0;l<fileList.GetSize();l++ )
            {
               strLine = fileList.GetAt(l); 
               
               item = m_listCtrl.InsertItem( m_listCtrl.GetItemCount(),strLine );
               m_listCtrl.SetItemText( item,1,strLine+".ret" );
            }
            fileList.RemoveAll();            
        }else
        {
            if ( _strcmpi( strLine.Right(4),".txt" )==0 )
            {
                item = m_listCtrl.InsertItem( m_listCtrl.GetItemCount(),strLine );
               m_listCtrl.SetItemText( item,1,strLine+".ret" );
            }
        }
	}
    */
	CDialog::OnDropFiles(hDropInfo);
}

void CRadCaliDlg::OnButtonRet() 
{
	UpdateData();
    CFileDialog dlg( TRUE,"txt",NULL,OFN_HIDEREADONLY|OFN_OVERWRITEPROMPT,"Txt File(*.txt)|*.txt|All File(*.*)|*.*||" );
    if ( dlg.DoModal()==IDOK )
	    m_strRet = dlg.GetPathName();
    UpdateData( FALSE );
}

void CRadCaliDlg::OnKeydownListCtrl(NMHDR* pNMHDR, LRESULT* pResult) 
{
    LV_KEYDOWN* pLVKeyDow = (LV_KEYDOWN*)pNMHDR;
    switch( pLVKeyDow->wVKey )
    {
    case VK_DELETE: 
        {
            POSITION pos = m_listCtrl.GetFirstSelectedItemPosition();
            while (pos != NULL){  
                m_listCtrl.DeleteItem( m_listCtrl.GetNextSelectedItem(pos) );
                pos = m_listCtrl.GetFirstSelectedItemPosition();
            }
        }
        break;
    case VK_INSERT: 
        break;        
    }
    *pResult = 0;
}

DWORD WINAPI WuProcThread( LPVOID lpParam ) 
{
	CRadCaliDlg *pDlg = (CRadCaliDlg *)lpParam;
	pDlg->Process();
    ::ExitThread( 0 );  return TRUE;
}


void CRadCaliDlg::OnButtonLoad() 
{
    UpdateData();
    if ( m_strRet.IsEmpty() ) return ;
    FILE *fSP = fopen( m_strRet,"rt" ); if (!fSP) return ;
    char strLn[1024],bsFL[256],mkFL[256],tgFL[256],scFL[256];
    fgets( strLn,256,fSP ); // skip // CBA correction parameters for aerial orthoimage data
    fgets( strLn,256,fSP ); sscanf( strLn,"%s",bsFL );   // BaseFileList.txt	// base image file list
    fgets( strLn,256,fSP ); sscanf( strLn,"%s",mkFL );   // MaskFileList.txt	// mask image file list
    fgets( strLn,256,fSP ); sscanf( strLn,"%s",tgFL );   // TargetFileList.txt	// target image file list
    fgets( strLn,256,fSP ); sscanf( strLn,"%s",scFL );   // sceneFileList.txt	// scene image file list
    fclose(fSP);
    
    char *pS,strDir[256],sT[256],strN[256]; int i,itm,sZ,sk,sr;
    strcpy( strDir,m_strRet ); pS = strrchr( strDir,'\\' ); if (pS) *pS=0;
    sprintf( strLn,"%s\\%s",strDir,scFL );
    FILE *fSc = fopen( strLn,"rt" ); if ( !fSc ) return ;    
    fgets( strLn,256,fSc ); sscanf( strLn,"%d%d%d",&sZ,&sk,&sr );
    fgets(strLn, 1024, fSc); fclose(fSc);
    sscanf(strLn, "%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s", sT, sT, sT, sT, sT, sT, sT, sT, sT, sT,
        sT, sT, sT, sT, sT, sT, sT, sT, sT, sT, sT, sT, sT, sT, sT, sT,strN );
    sprintf(strLn, "%s\\%s", strDir, strN);
    fSc = fopen(strLn, "rt"); if (!fSc) return;
    fgets(strLn, 256, fSc); // 50 3 49
    fgets(strLn, 256, fSc); // image  UTM Time Xs    Ys    Zs   Phi Omega Kappa MeanZ Rotation D/M/Y H:M:S
    
    m_listCtrl.DeleteAllItems();
    
    sprintf( strLn,"%s\\%s",strDir,bsFL );
    FILE *fBas = fopen( strLn,"rt" ); if ( !fBas ) return ;    
    fscanf( fBas,"%s%s%s%d%s",sT,sT,sT,&m_utmZn,bsFL  );
    fclose(fBas);
    
    m_strBas.Format("%s\\%s",strDir,bsFL);
    m_strDom = scFL;
        
    double ut, xs, ys, zs, phi, omg, kap, grdZ,rot;
    int utmZn,yy, mm, dd, ho, mi, se;
    for( i=0;i<sZ;i++ ){
        fgets( strLn,1024,fSc ); 
        sscanf( strLn,"%s%d%lf%lf%lf%lf%lf%lf%lf%lf%lf %d/%d/%d %d:%d:%d",
            strN,&utmZn,&ut, &xs, &ys, &zs, &phi, &omg, &kap, &grdZ,
            &rot, &dd, &mm, &yy, &ho, &mi, &se);

        sprintf( strLn,"%s\\%s",strDir,strN ); DOS_PATH(strLn);
        itm = m_listCtrl.InsertItem( m_listCtrl.GetItemCount(),strLn );
        
        ho += 2;   // just for posol bug.
        grdZ = 450;  // just for lecia-Dmc data
        
        sprintf(strLn, "%.3lf %.3lf %.3lf %.6lf %.6lf %.6lf %.2lf %d %d %d %d %d %d %d", xs, ys, zs,phi,omg,kap, grdZ,yy,mm,dd,ho,mi,se,utmZn );
        m_listCtrl.SetItemText(itm, 1, strLn);
    }
    fclose(fSc);
    
    AfxGetApp()->WriteProfileString( "CRadCaliDlg","Prj",m_strRet );
    // ── LAS 目录自动探测 ──────────────────────────────────────────────
    // 策略（优先级从高到低）：
    //   1. 项目文件同目录下的 "LAS" 子目录（最常见布局）
    //   2. 项目文件同目录下的 "LiDAR" / "las" 子目录（次常见别名）
    //   3. 上述位置均不存在则保留注册表中上次使用的路径
    // 如需手动指定，可在 .prj 文件第5行写 LAS=<dir>（覆盖自动探测）
    {
        char strLasT[512] = {};
        // 先尝试从项目文件第5行读 LAS=<dir>
        FILE* fPrj2 = fopen(m_strRet, "rt");
        if (fPrj2) {
            char ln[512] = {}; int L = 0;
            while (fgets(ln, sizeof(ln), fPrj2) && L < 5) {
                L++;
                if (_strnicmp(ln, "LAS=", 4) == 0 || _strnicmp(ln, "LAS:", 4) == 0) {
                    sscanf(ln + 4, "%s", strLasT); DOS_PATH(strLasT); break;
                }
            }
            fclose(fPrj2);
        }
        if (!strLasT[0]) {
            // 自动探测子目录
            const char* cands[] = { "LAS","las","LiDAR","lidar","Lidar","PointCloud",NULL };
            for (int k = 0; cands[k]; k++) {
                char t[512]; sprintf(t, "%s\\%s", strDir, cands[k]);
                WIN32_FIND_DATAA fd2;
                HANDLE h2 = FindFirstFileA((std::string(t) + "\\*.las").c_str(), &fd2);
                if (h2 == INVALID_HANDLE_VALUE)
                    h2 = FindFirstFileA((std::string(t) + "\\*.laz").c_str(), &fd2);
                if (h2 != INVALID_HANDLE_VALUE) { FindClose(h2); strcpy(strLasT, t); break; }
            }
        }
        if (strLasT[0]) {
            m_strLasDir = strLasT;
            cprintf("[OnButtonLoad] LAS dir: %s\n", strLasT);
        }
        else {
            // 从注册表恢复上次路径
            m_strLasDir = AfxGetApp()->GetProfileString("CRadCaliDlg", "LasDir", "");
            if (!m_strLasDir.IsEmpty())
                cprintf("[OnButtonLoad] LAS dir (registry): %s\n", (LPCSTR)m_strLasDir);
        }
        AfxGetApp()->WriteProfileString("CRadCaliDlg", "LasDir", m_strLasDir);
    }
    UpdateData(FALSE);
}


void CRadCaliDlg::OnOK() 
{
	UpdateData();
    
    if ( m_strRet.IsEmpty()||m_strBas.IsEmpty() ) return ;

    if ( m_listCtrl.GetItemCount()>0 && m_hThread==NULL  ){
        DWORD dwThreadId; m_hThread = ::CreateThread( NULL,0,WuProcThread,(void*)this,0,&dwThreadId );
        GetDlgItem(IDOK)->EnableWindow( m_hThread==NULL );
    }
}

int comRI(const void *pA,const void *pB){
    struct RI{ int idx; float area; };
    return int(((RI*)pB)->area-((RI*)pA)->area);
}

// IGG3权函数
inline double IGG3Weight(double std_residual, double init_weight, double k0 = 2.0, double k1 = 4.0) {
    double abs_std = fabs(std_residual);
    if (abs_std <= k0) {
        return init_weight;
    }
    else if (abs_std <= k1) {
        double ratio = (k1 - abs_std) / (k1 - k0);
        return init_weight * ratio * ratio;  // 平方衰减
    }
    else {
        return 0.01 * init_weight;  // 保留1%
    }
}
/*----------------------------------------------------------------------
 *  CTifAffine：单幅 TIFF 的仿射参数 + 整幅影像内存数据
 *  影像读取完全由 GDAL 完成，支持任意波段数和位深的 GeoTIFF。
 *----------------------------------------------------------------------*/
struct CTifAffine
{
    // 仿射参数（像元中心坐标系，与原 tfw 约定兼容）
    double Dx, Rx;      // 列方向 X、Y 增量
    double Ry, Dy;      // 行方向 X、Y 增量
    double Ex, Ny;      // 左上角像元中心地理坐标
    double det;         // = Dx*Dy - Ry*Rx

    int    cols, rows;
    int    bands;       // 波段数
    int    smpBytes;    // 每样本字节数（uint16 = 2）
    int    pxBytes;     // 每像素总字节数 = bands × smpBytes
    BYTE* pDat;        // BIP，rows × cols × pxBytes，行从顶到底

    // 影像原始坐标系 WKT（用于跨坐标系转换）
    std::string srsWkt;

    CTifAffine() : Dx(1), Rx(0), Ry(0), Dy(-1), Ex(0), Ny(0), det(-1),
        cols(0), rows(0), bands(0), smpBytes(2), pxBytes(0), pDat(NULL) {
    }
    ~CTifAffine() { Free(); }

    void Free() { if (pDat) { delete[] pDat; pDat = NULL; } cols = rows = 0; }


    // 影像的完整地理包围盒（xmin, xmax, ymin, ymax）
    void GetBBox(double& xmin, double& xmax,
        double& ymin, double& ymax) const
    {
        double xs[4], ys[4];
        Pix2Geo(0, 0, xs[0], ys[0]);
        Pix2Geo(cols - 1, 0, xs[1], ys[1]);
        Pix2Geo(0, rows - 1, xs[2], ys[2]);
        Pix2Geo(cols - 1, rows - 1, xs[3], ys[3]);
        xmin = *std::min_element(xs, xs + 4);
        xmax = *std::max_element(xs, xs + 4);
        ymin = *std::min_element(ys, ys + 4);
        ymax = *std::max_element(ys, ys + 4);
    }

    // 加载 GeoTIFF（GDAL 读取）
    bool Load(const char* path)
    {
        Free();

        // 打开数据集
        GDALAllRegister();
        GDALDataset* ds = (GDALDataset*)GDALOpen(path, GA_ReadOnly);
        if (!ds) {
            cprintf("[CTifAffine::Load] GDALOpen failed: %s\n", path);
            return false;
        }

        // 读取坐标系
        const char* wkt = ds->GetProjectionRef();
        srsWkt = (wkt && wkt[0]) ? wkt : "";

        // 读取仿射变换
        double gt[6] = { 0,1,0,0,0,-1 };
        if (ds->GetGeoTransform(gt) != CE_None) {
            cprintf("[CTifAffine::Load] WARNING: no GeoTransform in %s, trying .tfw\n", path);
            char strTfw[512]; strcpy(strTfw, path);
            char* pExt = strrchr(strTfw, '.'); if (pExt) strcpy(pExt, ".tfw"); else strcat(strTfw, ".tfw");
            FILE* f = fopen(strTfw, "rt");
            if (f) {
                double tfwDx, tfwRx, tfwRy, tfwDy, tfwEx, tfwNy;
                if (fscanf(f, "%lf%lf%lf%lf%lf%lf", &tfwDx, &tfwRx, &tfwRy, &tfwDy, &tfwEx, &tfwNy) == 6) {
                    gt[0] = tfwEx - tfwDx * 0.5; gt[1] = tfwDx; gt[2] = tfwRy;
                    gt[3] = tfwNy - tfwDy * 0.5; gt[4] = tfwRx; gt[5] = tfwDy;
                }
                fclose(f);
            }
        }
        // GDAL gt（左上角）→ 像元中心仿射参数
        Dx = gt[1]; Rx = gt[4];
        Ry = gt[2]; Dy = gt[5];
        Ex = gt[0] + gt[1] * 0.5 + gt[2] * 0.5;
        Ny = gt[3] + gt[4] * 0.5 + gt[5] * 0.5;
        det = Dx * Dy - Ry * Rx;
        if (fabs(det) < 1e-20) { GDALClose(ds); return false; }

        // 影像尺寸与波段规格
        cols = ds->GetRasterXSize();
        rows = ds->GetRasterYSize();
        bands = ds->GetRasterCount();
        if (cols <= 0 || rows <= 0 || bands <= 0) { GDALClose(ds); return false; }

        GDALDataType dt = ds->GetRasterBand(1)->GetRasterDataType();
        smpBytes = 2;
        pxBytes = bands * smpBytes;

        // RasterIO 读全图 BIP uint16
        LONGLONG sz = LONGLONG(rows) * cols * pxBytes;
        pDat = new BYTE[sz + 64];
        memset(pDat, 0, sz + 64);

        CPLErr err = ds->RasterIO(
            GF_Read, 0, 0, cols, rows,
            pDat, cols, rows, GDT_UInt16, bands, NULL,
            (int)pxBytes,
            (int)(LONGLONG(cols) * pxBytes),
            (int)smpBytes
        );
        GDALClose(ds);

        if (err != CE_None) {
            cprintf("[CTifAffine::Load] RasterIO failed: %s\n", path);
            Free(); return false;
        }
        return true;
    }

    // ── 坐标变换 ──────────────────────────────────────────────────

    // 像素（col,row，顶左=0,0）→ 地理（X,Y）
    inline void Pix2Geo(double col, double row, double& X, double& Y) const {
        X = Ex + Dx * col + Ry * row;
        Y = Ny + Rx * col + Dy * row;
    }

    // 地理（X,Y）→ 像素（col,row，浮点，可能越界）
    inline void Geo2Pix(double X, double Y, double& col, double& row) const {
        double dx = X - Ex, dy = Y - Ny;
        col = (Dy * dx - Ry * dy) / det;
        row = (Dx * dy - Rx * dx) / det;
    }

    // 像元面积开方，近似像元大小（单位与地理坐标一致）
    inline double GSD() const { return sqrt(fabs(det)); }

    // ── 像素访问 ─────────────────────────────────────────────────

    inline const WORD* PixPtr(int col, int row) const {
        return reinterpret_cast<const WORD*>(pDat + (LONGLONG(row) * cols + col) * pxBytes);
    }

    // 判断全黑（前 min(bands,3) 个波段均为 0）
    static inline bool IsBlack(const WORD* px, int b) {
        int n = b < 3 ? b : 3;
        for (int i = 0; i < n; i++) if (px[i] != 0) return false;
        return true;
    }

    // ── 采样 ─────────────────────────────────────────────────────

    // 双线性插值
    bool BilinearSample(double fc, double fr, WORD* out) const {
        int c0 = int(fc), r0 = int(fr);
        if (c0 < 0 || r0 < 0 || c0 >= cols - 1 || r0 >= rows - 1) return false;
        float dx = float(fc - c0), dy = float(fr - r0);
        const WORD* p00 = PixPtr(c0, r0), * p10 = PixPtr(c0 + 1, r0);
        const WORD* p01 = PixPtr(c0, r0 + 1), * p11 = PixPtr(c0 + 1, r0 + 1);
        for (int b = 0; b < bands; b++)
            out[b] = WORD((1 - dx) * (1 - dy) * p00[b] + dx * (1 - dy) * p10[b] + (1 - dx) * dy * p01[b] + dx * dy * p11[b]);
        return true;
    }

    // 块平均采样（avz×avz 窗口，avz 取奇数）
    bool AvgSample(double fc, double fr, int avz, WORD* out) const {
        int c0 = int(fc + 0.5), r0 = int(fr + 0.5), az = avz / 2;
        if (c0 - az < 0 || r0 - az < 0 || c0 + az >= cols || r0 + az >= rows) return false;
        INT64 sv[16] = {}; int cnt[16] = {};
        for (int dr = -az; dr <= az; dr++) {
            const BYTE* rp = pDat + LONGLONG(r0 + dr) * cols * pxBytes;
            for (int dc = -az; dc <= az; dc++) {
                const WORD* px = reinterpret_cast<const WORD*>(rp + LONGLONG(c0 + dc) * pxBytes);
                for (int b = 0; b < bands; b++) if (px[b] > 0) { sv[b] += px[b]; cnt[b]++; }
            }
        }
        for (int b = 0; b < bands; b++) out[b] = cnt[b] ? WORD(sv[b] / cnt[b]) : 0;
        return true;
    }

    // 通用采样：gs==0 双线性，gs>0 块平均
    bool Sample(double fc, double fr, int gs, WORD* out) const {
        return (gs > 0) ? AvgSample(fc, fr, gs, out) : BilinearSample(fc, fr, out);
    }

    // 根据地理坐标采样
    bool SampleGeo(double gx, double gy, int gs, WORD* out,
        double* outCol = NULL, double* outRow = NULL) const {
        double fc, fr; Geo2Pix(gx, gy, fc, fr);
        if (outCol) *outCol = fc;
        if (outRow) *outRow = fr;
        return Sample(fc, fr, gs, out);
    }
};

struct OlpInfo {
    char   path[512];
    int    idx;
    int    idxr;
    double initWeight;  // Excellent=1.0, Good=0.8, Fair=0.5, Poor=0.2
};
static void CollectOlpList(int numImages, const char* const* tskFiles,
    std::vector<OlpInfo>& olpList)
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
            OlpInfo info; strcpy(info.path, strOlp);
            info.idx = idx; info.idxr = idxr;
            info.initWeight = (idxr == -1) ? 1.0 : 0.1;  // 默认权重
            olpList.push_back(info);
        }
        fclose(fTsk);
    }
}

static void CollectOlpListWithQuality(
    int numImages,
    const char* const* tskFiles,
    const std::vector<const char*>& excellentFiles,
    const std::vector<const char*>& goodFiles,
    const std::vector<const char*>& fairFiles,
    const std::vector<const char*>& poorFiles,
    std::vector<OlpInfo>& olpList)
{
    olpList.clear();

    bool useQuality = !excellentFiles.empty() || !goodFiles.empty() ||
        !fairFiles.empty() || !poorFiles.empty();

    if (!useQuality) {
        // 向后兼容：没有质量文件则读原始 .olp
        CollectOlpList(numImages, tskFiles, olpList);
        return;
    }

    struct QualEntry { const std::vector<const char*>* files; double w; const char* suffix; };
    QualEntry quals[] = {
        { &excellentFiles, 1.00, "_excellent.olp" },
        { &goodFiles,      0.80, "_good.olp"      },
        { &fairFiles,      0.50, "_fair.olp"      },
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

                OlpInfo info;
                strcpy(info.path, qualPath);
                info.idx = idx;
                info.idxr = idxr;
                info.initWeight = qe.w * ((idxr == -1) ? 1.0 : 0.1);
                olpList.push_back(info);
                anyQualFound = true;
            }

            // 如果该配对完全没有质量文件，回退到原始 .olp（保证不丢数据）
            if (!anyQualFound) {
                FILE* ft = fopen(strOlp, "rb");
                if (ft) {
                    fclose(ft);
                    OlpInfo info;
                    strcpy(info.path, strOlp);
                    info.idx = idx;
                    info.idxr = idxr;
                    info.initWeight = (idxr == -1) ? 1.0 : 0.1;
                    olpList.push_back(info);
                }
            }
        }
        fclose(fTsk);
    }
}

// 导出匹配点到JSON
static void ExportMatchPointsToJSON(
    const char* filename,
    const std::vector<OlpInfo>& olpList,
    int numOlpFiles)
{
    FILE* f = fopen(filename, "wt");
    if (!f) {
        printf("Failed to create %s\n", filename);
        return;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"matches\": [\n");

    int pointCount = 0;

    for (int fi = 0; fi < numOlpFiles; fi++) {
        const OlpInfo& info = olpList[fi];
        COlpFile olpF;
        if (!olpF.Load4File(info.path)) continue;

        int oz;
        OBV* pOs = olpF.GetData(&oz);

        for (int vi = 0; vi < oz; vi++, pOs++) {
            if (pointCount > 0) fprintf(f, ",\n");

            fprintf(f, "    {\n");
            fprintf(f, "      \"img1_id\": %d,\n", info.idx);
            fprintf(f, "      \"img2_id\": %d,\n", info.idxr == -1 ? -1 : info.idxr);

            // 使用实际的字段名：cc, cr, rc, rr
            fprintf(f, "      \"x1\": %d,\n", pOs->cc);
            fprintf(f, "      \"y1\": %d,\n", pOs->cr);
            fprintf(f, "      \"x2\": %d,\n", pOs->rc);
            fprintf(f, "      \"y2\": %d,\n", pOs->rr);

            // 质量等级（根据权重推断）
            int quality = 3;  // 默认Good
            if (info.initWeight >= 0.95) quality = 4;      // Excellent
            else if (info.initWeight >= 0.75) quality = 3; // Good
            else if (info.initWeight >= 0.45) quality = 2; // Fair
            else quality = 1;                              // Poor

            fprintf(f, "      \"quality\": %d,\n", quality);
            fprintf(f, "      \"ncc\": %.4f,\n", info.initWeight);

            // DN差异（使用第一个波段）
            double dn_diff = fabs(pOs->cv[0] - pOs->rv[0]);
            fprintf(f, "      \"dn_diff\": %.2f\n", dn_diff);

            fprintf(f, "    }");
            pointCount++;

            // 限制导出数量（避免文件过大）
            if (pointCount >= 100000) goto done_export;
        }
    }

done_export:
    fprintf(f, "\n  ],\n");
    fprintf(f, "  \"total_points\": %d\n", pointCount);
    fprintf(f, "}\n");
    fclose(f);

    printf("✓ Exported %d match points to %s\n", pointCount, filename);
}

// 导出残差到JSON
static void ExportResidualsToJSON(
    const char* filename,
    const std::vector<OlpInfo>& olpList,
    const std::vector<double>& obsWeights,
    const std::vector<int>& fileOidStart,
    int numOlpFiles,
    int band_idx,
    int iteration)
{
    FILE* f = fopen(filename, "wt");
    if (!f) return;

    fprintf(f, "{\n");
    fprintf(f, "  \"iteration\": %d,\n", iteration);
    fprintf(f, "  \"band\": %d,\n", band_idx + 1);
    fprintf(f, "  \"residuals\": [\n");

    int resCount = 0;

    for (int fi = 0; fi < numOlpFiles; fi++) {
        const OlpInfo& info = olpList[fi];
        COlpFile olpF;
        if (!olpF.Load4File(info.path)) continue;

        int oz;
        OBV* pOs = olpF.GetData(&oz);
        int oidBase = fileOidStart[fi];

        for (int vi = 0; vi < oz; vi++, pOs++) {
            int oid = oidBase + vi;
            double w = (oid < (int)obsWeights.size()) ? obsWeights[oid] : 0.0;

            if (w < 0.001) continue;

            double cv = pOs->cv[band_idx];
            double rv = pOs->rv[band_idx];
            double initial_diff = cv - rv;

            if (resCount > 0) fprintf(f, ",\n");

            fprintf(f, "    {\n");
            fprintf(f, "      \"img1_id\": %d,\n", info.idx);
            fprintf(f, "      \"img2_id\": %d,\n", info.idxr);
            fprintf(f, "      \"x\": %d,\n", pOs->cc);  // 使用cc作为x坐标
            fprintf(f, "      \"y\": %d,\n", pOs->cr);  // 使用cr作为y坐标
            fprintf(f, "      \"initial_diff\": %.2f,\n", initial_diff);
            fprintf(f, "      \"residual\": %.2f,\n", initial_diff);  // 简化版本
            fprintf(f, "      \"weight\": %.4f\n", w);
            fprintf(f, "    }");

            resCount++;

            // 限制导出数量
            if (resCount >= 50000) goto done_residuals;
        }
    }

done_residuals:
    fprintf(f, "\n  ]\n");
    fprintf(f, "}\n");
    fclose(f);

    printf("✓ Exported %d residuals to %s\n", resCount, filename);
}

void CRadCaliDlg::Process()
{
    gs_hWnd = m_hWnd;
	m_hEThEHdl = ::CreateEvent( NULL,TRUE,FALSE,itostr( LONG(this) ) );
    
    CTime stTm; SYSTEM_INFO sysInfo; GetSystemInfo (&sysInfo);   
    if (sysInfo.dwNumberOfProcessors>MAX_CPU) sysInfo.dwNumberOfProcessors=MAX_CPU;
    BOOL bRun = FALSE; int maxTm=120,cpuSum = atoi( m_strMxCore ); 
    if (cpuSum<1) cpuSum = 1;  if (cpuSum>sysInfo.dwNumberOfProcessors-1) cpuSum = sysInfo.dwNumberOfProcessors-1;
    char strCmd[1024],strExe[256]; ::GetModuleFileName(NULL,strExe,sizeof(strExe));

    m_strMxCore.Format("%d",cpuSum );
    AfxGetApp()->WriteProfileString( "CRadCaliDlg","CPUs",m_strMxCore );  

    char *pS,strDir[256],strT[256],str[512];
    strcpy( strDir,m_strRet ); pS = strrchr( strDir,'\\' ); if (pS) *pS=0;
    
    char strRom[256]; sprintf( strRom,"%s\\ROM_S2Bexp_0.02m",strDir );
    CreateDir( strRom );

    char strLog[256]; strcpy( strLog,m_strRet );
    sprintf( str,"%s-s2b.log",strLog ); 
    ::DeleteFile(str);  openLog(str);  

    int i,j,c,b,sum = m_listCtrl.GetItemCount();
    ///////////////////////////////////    
    struct RI{
        int idx;
        float area;
    }; RI iRi,*pRi = new RI[sum+8];
    struct RGN{
        double x[8];
        double y[8];
        int sz;
    }; RGN iRg,*pRg = new RGN[sum+8];
    memset( pRg,0,sizeof(RGN)*sum );
    IMGPAR imgPar; CTMGeom cc; 
    double xs,ys,zs,phi,omg,kap,grdZ;
    sprintf( strT,"%s\\imgPar.dpi",strDir ); DOS_PATH(strT); 
    CTMVziFile vziFile; vziFile.Load4File(strT); 
    for( i=0;i<sum;i++ ){
        m_listCtrl.GetItemText(i,1,str,256);
        imgPar = vziFile.m_imgPar;
        sscanf( str,"%lf%lf%lf%lf%lf%lf%lf",&xs,&ys,&zs,&phi,&omg,&kap,&grdZ );        
        imgPar.aopX = xs; imgPar.aopY = ys; imgPar.aopZ = zs;
        imgPar.aopP = phi;imgPar.aopO = omg;imgPar.aopK = kap;
        cc.Init( &imgPar ); pRg[i].sz = 4;
        cc.GetGrdPrjRgn( imgPar.iopX*2,imgPar.iopY*2,grdZ,pRg[i].x,pRg[i].y );
    }

    // ── LAS 目录（写入任务文件供 MchTie 使用）──────────────────────
    char strLasDir[512] = {};
    if (!m_strLasDir.IsEmpty())
        strcpy(strLasDir, (LPCSTR)m_strLasDir);

    if (m_bTie) print2Log( "MchTie start...\n" );
    ///////////////////////////////////    
    ProgBegin(sum); int cancel;
    for( i=0;i<sum;i++,ProgStep(cancel) )
	{
		if ( ::WaitForSingleObject(m_hEThEHdl,1)==WAIT_OBJECT_0 ) break;

        m_listCtrl.GetItemText(i,0,str,256);  print2Log("process: %s\n",strrchr(str,'\\') );
        sprintf( strT,"%s%s.tsk",strRom,strrchr(str,'\\') );
        FILE *fTsk = fopen( strT,"wt" );
        fprintf( fTsk,"%s\n%d %s %d %d %d\n",str,i,m_listCtrl.GetItemText(i,1),m_gs,m_ws,m_bTxt );
        // LAS 目录
        if (strLasDir[0])
            fprintf(fTsk, "LAS=%s\n", strLasDir);

        fprintf( fTsk,"%s\n%s\n",m_strBas,"-1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 " );

        /////////////////////////////////////////////
        iRi.area = (float)(GetRgnArea( pRg[i].x,pRg[i].y,pRg[i].sz ));
        memset( pRi,0,sizeof(RI)*sum );
        for( j=0;j<sum;j++ ){
            if ( j==i ) continue;
            iRg.sz = 0;
            RgnClip( pRg[i].x,pRg[i].y,pRg[i].sz,
                pRg[j].x,pRg[j].y,pRg[j].sz,
                iRg.x,iRg.y,&iRg.sz );
            if ( iRg.sz>2 ){
                pRi[j].idx = j;
                pRi[j].area = (float)(GetRgnArea( iRg.x,iRg.y,iRg.sz ));
        }
    }
        qsort( pRi,sum,sizeof(RI),&comRI );
        for( j=0;j<sum;j++ ){
            if ( pRi[j].area/iRi.area<0.01  ) break;

            fprintf( fTsk,"%s\n",m_listCtrl.GetItemText( pRi[j].idx,0 ) );
            fprintf( fTsk,"%d %s\n",pRi[j].idx,m_listCtrl.GetItemText( pRi[j].idx,1 ) );
        }
        /////////////////////////////////////////////
        fclose( fTsk );

        if (m_bTie){
#ifdef _DEBUG
            sprintf(strCmd, "TIE@%s",strT ); 
            MchTie( strCmd ); 
            //break;
#else
            sprintf(strCmd, "%s TIE@%s",strExe,strT ); 
            print2Log( "%s\n",strCmd );
#endif

            bRun = FALSE; 
            while( !bRun ){
                for (int c=0;c<cpuSum;c++ ){
                    if ( gs_hThreadId[c]==NULL ){
                        strcpy( gs_strCmd[c],strCmd );
                        HANDLE hThread = ::CreateThread( NULL,0,CupThread_CRadCaliDlg,(void*)gs_strCmd[c],0,gs_hThreadId+c );
                        ::CloseHandle( hThread ); bRun = TRUE; break;
                    }
                    if ( ::WaitForSingleObject(m_hEThEHdl,16)==WAIT_OBJECT_0 ) break;
                }
            }
        }   
	}
    delete []pRi;
    delete []pRg;

    // waiting all process over in maxTm 
    stTm = CTime::GetCurrentTime();
    while( 1 ){
        for ( c=0;c<MAX_CPU;c++ ){ if ( gs_hThreadId[c]!=NULL ) break; }
        if ( c==MAX_CPU || ::WaitForSingleObject(m_hEThEHdl,128)==WAIT_OBJECT_0 ) break;
        // Terminate the process
        CTimeSpan ts = CTime::GetCurrentTime()-stTm;
        if ( ts.GetTotalMinutes()>maxTm ){
            for ( int i=0;i<MAX_CPU;i++ ){
                if ( gs_hProcId[i] ){
                    HANDLE hProc = ::OpenProcess( PROCESS_TERMINATE,FALSE,gs_hProcId[i] );
                    if ( hProc ) ::TerminateProcess( hProc,0x22 );
                    gs_hProcId[i] = 0; Sleep(8);
                }
            }
            break;
        }
    }
    if (m_bTie) print2Log( "MchTie over.\n" );
    ProgEnd();  

    /////////////////////////////////
    COlpFile olpF; char strSrc[256],strRef[256],strOlp[512]; int idx,idxr;
    if (m_bAdj) {
        print2Log("RadBA start...\n");

        double* pAK1 = new double[sum * 2];
        double* pAK2 = pAK1 + sum;
        memset(pAK1, 0, sizeof(double) * sum * 2);
        for (i = 0; i < sum; i++) {
            m_listCtrl.GetItemText(i, 0, str, 256);
            sprintf(strT, "%s%s.tsk_skm.txt", strRom, strrchr(str, '\\'));
            FILE* fKM = fopen(strT, "rt");
            fscanf(fKM, "%lf%lf", pAK1 + i, pAK2 + i);   print2Log("%lf %lf\n", pAK1[i], pAK2[i]);
            fclose(fKM);
        }

        int n = sum * 4;
        print2Log("System size: %d unknowns\n", n);

        char** tskFileList = new char* [sum];
        for (i = 0; i < sum; i++) {
            m_listCtrl.GetItemText(i, 0, str, 256);
            tskFileList[i] = new char[512];
            sprintf(tskFileList[i], "%s%s.tsk", strRom, strrchr(str, '\\'));
        }

        ProgBegin(sum * 4);

        std::vector<OlpInfo> olpList;
        std::vector<int>fileOidStart;
        for (c = 0; c < 4; c++) {
            UINT st = GetTickCount();
            print2Log("\n========== Band %d ==========\n", c + 1);

            print2Log("Step 1: Counting observations...\n");
            // 为此波段的所有参考影像收集匹配点
            std::vector<const char*> excellentFiles, goodFiles, fairFiles, poorFiles;

            // 从任务文件扫描生成对应的质量文件路径
            for (i = 0; i < sum; i++) {
                char str[512], strT[256];
                m_listCtrl.GetItemText(i, 0, str, 256);

                // 生成该影像对应的质量分级文件路径
                sprintf(strT, "%s%s.tsk", strRom, strrchr(str, '\\'));

                // 打开任务文件，逐行读参考影像
                FILE* fTsk = fopen(strT, "rt");
                if (!fTsk) continue;

                char strLine[512], strSrc[256];
                int idx;

                // 第1行：源影像
                if (!fgets(strLine, sizeof(strLine), fTsk)) { fclose(fTsk); continue; }
                sscanf(strLine, "%s", strSrc);

                // 第2行：影像索引
                if (!fgets(strLine, sizeof(strLine), fTsk)) { fclose(fTsk); continue; }
                sscanf(strLine, "%d", &idx);

                // 可能的第3行：LAS 目录（检查并跳过）
                {
                    long pos = ftell(fTsk);
                    if (fgets(strLine, sizeof(strLine), fTsk)) {
                        char tmp[512] = {};
                        sscanf(strLine, "%s", tmp);
                        if (_strnicmp(tmp, "LAS=", 4) != 0 && _strnicmp(tmp, "LAS:", 4) != 0) {
                            fseek(fTsk, pos, SEEK_SET);  // 回退
                        }
                    }
                }

                // 逐参考影像处理
                while (!feof(fTsk)) {
                    if (!fgets(strLine, sizeof(strLine), fTsk)) break;
                    sscanf(strLine, "%s", strRef);
                    DOS_PATH(strRef);

                    if (!fgets(strLine, sizeof(strLine), fTsk)) break;
                    int idxr;
                    sscanf(strLine, "%d", &idxr);

                    // ✅ 生成质量文件路径（与 CollectOlpList 逻辑完全一致）
                    char strOlp[512];
                    strcpy(strOlp, strT);                          // 先用任务文件路径
                    strcpy(strrchr(strOlp, '.'), "_");             // 把 .tsk 改为 _
                    strcat(strOlp, strrchr(strRef, '\\') + 1);     // 加参考影像文件名
                    strcat(strOlp, ".olp");                        // 加 .olp

                    // 生成质量分级文件路径
                    char strExc[512], strGd[512], strFr[512], strPr[512];
                    strcpy(strExc, strOlp);
                    char* p = strrchr(strExc, '.'); if (p) strcpy(p, "_excellent.olp");

                    strcpy(strGd, strOlp);
                    p = strrchr(strGd, '.'); if (p) strcpy(p, "_good.olp");

                    strcpy(strFr, strOlp);
                    p = strrchr(strFr, '.'); if (p) strcpy(p, "_fair.olp");

                    strcpy(strPr, strOlp);
                    p = strrchr(strPr, '.'); if (p) strcpy(p, "_poor.olp");

                    // 将存在的文件加入对应列表
                    if (IsExist(strExc)) excellentFiles.push_back(_strdup(strExc));
                    if (IsExist(strGd)) goodFiles.push_back(_strdup(strGd));
                    if (IsExist(strFr)) fairFiles.push_back(_strdup(strFr));
                    if (IsExist(strPr)) poorFiles.push_back(_strdup(strPr));
                }
                fclose(fTsk);
            }

            // 统计信息
            int nEx = 0, nGd = 0, nFr = 0, nPr = 0;
            for (auto& f : excellentFiles) {
                COlpFile tmp;
                if (tmp.Load4File(f)) nEx += tmp.GetSize();
            }
            for (auto& f : goodFiles) {
                COlpFile tmp;
                if (tmp.Load4File(f)) nGd += tmp.GetSize();
            }
            for (auto& f : fairFiles) {
                COlpFile tmp;
                if (tmp.Load4File(f)) nFr += tmp.GetSize();
            }
            for (auto& f : poorFiles) {
                COlpFile tmp;
                if (tmp.Load4File(f)) nPr += tmp.GetSize();
            }

            print2Log("  Quality distribution: Excellent=%d, Good=%d, Fair=%d, Poor=%d (Total=%d)\n",
                nEx, nGd, nFr, nPr, nEx + nGd + nFr + nPr);

            print2Log("  Strategy: Merged with quality weights\n");
            print2Log("    Excellent: 1.0x (最可靠)\n");
            print2Log("    Good:      0.8x (良好)\n");
            print2Log("    Fair:      0.5x (一般)\n");
            print2Log("    Poor:      0.2x (低信任)\n");

            CollectOlpListWithQuality(sum, tskFileList,
                excellentFiles, goodFiles, fairFiles, poorFiles,
                olpList);

            int numOlpFiles = (int)olpList.size();

            if (c == 0) {  // 只在第一个波段导出一次
                char match_json[512];
                sprintf(match_json, "%smatch_points.json", strRom);
                ExportMatchPointsToJSON(match_json, olpList, numOlpFiles);
            }

            // 预计算各文件在 obsWeights 中的起始偏移，并统计总观测数
            fileOidStart.resize(numOlpFiles + 1, 0);
            int totalObs = 0;
            for (int fi = 0; fi < numOlpFiles; fi++) {
                fileOidStart[fi] = totalObs;
                COlpFile olpF;
                if (olpF.Load4File(olpList[fi].path)) {
                    int oz; olpF.GetData(&oz); totalObs += oz;
                }
                fileOidStart[fi + 1] = totalObs;
            }

            if (totalObs == 0) {
                print2Log("  ERROR: No observations found in quality OLP files!\n");
                return;
            }
            std::vector<double> obsWeights(totalObs);
            std::vector<double> initWeights(totalObs);
            {
                int oid = 0;
                for (int fi = 0; fi < numOlpFiles; fi++) {
                    COlpFile olpF;
                    if (!olpF.Load4File(olpList[fi].path)) continue;
                    int oz; olpF.GetData(&oz);
                    double w0 = olpList[fi].initWeight;  // ★ 质量权重
                    for (int vi = 0; vi < oz; vi++, oid++)
                        obsWeights[oid] = initWeights[oid] = 1.0/*w0*/;
                }
            }

            // ===== 迭代平差 =====
            int max_iterations = 2;
            double sigma0 = 1.0;

            VectorXd x = VectorXd::Zero(n);

            for (int iter = 0; iter < max_iterations; iter++) {
                print2Log("\n--- Iteration %d ---\n", iter + 1);
                UINT iter_st = GetTickCount();

                print2Log("  Building equations (streaming)...");

                MatrixXd AtWA = MatrixXd::Zero(n, n);
                VectorXd AtWb = VectorXd::Zero(n);

                int global_obs_id = 0;
                int processed_obs = 0;

                for (int fi = 0; fi < numOlpFiles; fi++) {
                    const OlpInfo& info = olpList[fi];
                    COlpFile olpF; if (!olpF.Load4File(info.path)) continue;
                    int oz; OBV* pOs = olpF.GetData(&oz);
                    int idx = info.idx, idxr = info.idxr;
                    int oidBase = fileOidStart[fi];

                    for (int vi = 0; vi < oz; vi++, pOs++) {
                        int oid = oidBase + vi;
                        double w = (oid < (int)obsWeights.size()) ? obsWeights[oid] : 0.0;
                        //if (w < 1e-3) continue;
                        double k1 = getKval(1, pOs->csz, pOs->cvz, pOs->cas)/* - pAK1[idx]*/;
                        double k2 = getKval(4, pOs->csz, pOs->cvz, pOs->cas)/* - pAK2[idx]*/;
                        double cv = pOs->cv[c]/* / DN_SCALE*/;
                        double rv = pOs->rv[c]/* / DN_SCALE*/;
                        if (idxr == -1) {
                            // 基线约束: -x0 - k1*x1 - k2*x2 + cv*x3 = rv
                            double a[4] = { -1.0, -k1, -k2, cv };
                            double l = rv;

                            int base_idx = idx * 4;

                            // 累加到法方程（4x4块）
                            for (int ii = 0; ii < 4; ii++) {
                                int row = base_idx + ii;
                                AtWb(row) += w * a[ii] * l;

                                for (int jj = 0; jj < 4; jj++) {
                                    int col = base_idx + jj;
                                    AtWA(row, col) += w * a[ii] * a[jj];
                                }
                            }
                        }

                        else {
                            double k1r = getKval(1, pOs->rsz, pOs->rvz, pOs->ras);
                            double k2r = getKval(4, pOs->rsz, pOs->rvz, pOs->ras);
                            double rv = pOs->rv[c];
                            // 相对约束：同名点校正后反射率相等
                            // val_i = a0_i + k1_i*a1_i + k2_i*a2_i - cv*s_i
                            // val_j = a0_j + k1j*a1_j + k2j*a2_j - rv*s_j
                            // 约束：val_i - val_j = 0
                            // 偏导（系数向量）与 val 定义一致
                            double a_idx[4] = { 1.0,  k1,   k2,  0 };
                            double a_idxr[4] = { -1.0, -k1r, -k2r,  0 };
                            double l = cv - rv;

                            int base_idx = idx * 4;
                            int base_idxr = idxr * 4;

                            // 当前影像块（4x4）
                            for (int ii = 0; ii < 4; ii++) {
                                int row = base_idx + ii;
                                AtWb(row) += w * a_idx[ii] * l;

                                for (int jj = 0; jj < 4; jj++) {
                                    int col = base_idx + jj;
                                    AtWA(row, col) += w * a_idx[ii] * a_idx[jj];
                                }
                            }

                            // 参考影像块（4x4）
                            for (int ii = 0; ii < 4; ii++) {
                                int row = base_idxr + ii;
                                AtWb(row) += w * a_idxr[ii] * l;

                                for (int jj = 0; jj < 4; jj++) {
                                    int col = base_idxr + jj;
                                    AtWA(row, col) += w * a_idxr[ii] * a_idxr[jj];
                                }
                            }
                            // 交叉块（4x4 x 2）
                            for (int ii = 0; ii < 4; ii++) {
                                for (int jj = 0; jj < 4; jj++) {
                                    int row1 = base_idx + ii;
                                    int col1 = base_idxr + jj;
                                    AtWA(row1, col1) += w * a_idx[ii] * a_idxr[jj];

                                    int row2 = base_idxr + ii;
                                    int col2 = base_idx + jj;
                                    AtWA(row2, col2) += w * a_idxr[ii] * a_idx[jj];
                                }
                            }
                        }
                        processed_obs++;
                    }
                }
                            
                print2Log("Done (%d obs)\n", processed_obs);

                // ===== 诊断法方程 =====
                print2Log("  Diagnosing normal equations...\n");

                // 检查对角线元素
                double diag_min = 1e100, diag_max = -1e100, diag_sum = 0.0;
                int zero_diag = 0, small_diag = 0, diag_count = 0;

                for (int ii = 0; ii < n; ii++) {
                    double d = AtWA(ii, ii);

                    if (fabs(d) < 1e-15) {
                        zero_diag++;
                    }
                    else if (fabs(d) < 1e-6) {
                        small_diag++;
                    }

                    if (d > 1e-15) {
                        if (d < diag_min) diag_min = d;
                        if (d > diag_max) diag_max = d;
                        diag_sum += d;
                        diag_count++;
                    }
                }

                double diag_avg = (diag_count > 0) ? (diag_sum / diag_count) : 1.0;
                double condition_est = (diag_min > 0) ? (diag_max / diag_min) : 1e20;

                print2Log("    Diagonal: min=%.2e, max=%.2e, avg=%.2e\n",
                    diag_min, diag_max, diag_avg);
                print2Log("    Condition estimate: %.2e\n", condition_est);
                print2Log("    Zero diagonals: %d, Small diagonals: %d\n",
                    zero_diag, small_diag);

                if (zero_diag > 0) {
                    print2Log("    ERROR: %d zero diagonal elements! Check for isolated images.\n", zero_diag);
                }

                if (condition_est > 1e12) {
                    print2Log("    WARNING: Matrix is very ill-conditioned!\n");
                }

                // 检查对称性
                double sym_error = 0.0;
                for (int ii = 0; ii < n; ii++) {
                    for (int jj = ii + 1; jj < n; jj++) {
                        sym_error += fabs(AtWA(ii, jj) - AtWA(jj, ii));
                    }
                }
                print2Log("    Symmetry error: %.2e\n", sym_error);
                // ===== 添加正则化 =====
                //print2Log("  Adding regularization...");

                //std::vector<double> valid_diags;
                //for (int ii = 0; ii < n; ii++) {
                //    double d = AtWA(ii, ii);
                //    if (d > 1e-15) {
                //        valid_diags.push_back(d);
                //    }
                //}

                //double diag_median = 1.0;
                //if (!valid_diags.empty()) {
                //    std::sort(valid_diags.begin(), valid_diags.end());
                //    diag_median = valid_diags[valid_diags.size() / 2];
                //}

                //// 自适应正则化：基于中位数
                //double reg = diag_median * 1e-6;

                //// 特殊处理：对零或极小的对角元素
                //for (int ii = 0; ii < n; ii++) {
                //    double d = AtWA(ii, ii);

                //    if (fabs(d) < 1e-15) {
                //        // 零对角线：给一个基准正则化
                //        AtWA(ii, ii) = diag_median * 0.01;
                //    }
                //    else if (d < diag_median * 0.01) {
                //        // 极小对角线：强正则化
                //        AtWA(ii, ii) += reg * 100.0;
                //    }
                //    else {
                //        // 正常对角线：标准正则化
                //        AtWA(ii, ii) += reg;
                //    }
                //}

                //print2Log("Done (reg=%.2e, median=%.2e)\n", reg, diag_median);

                // ===== 求解（使用LDLT）=====
                print2Log("  Solving system (LDLT)...");

                Eigen::LDLT<MatrixXd> ldlt(AtWA);

                //if (ldlt.info() != Eigen::Success) {
                //    print2Log("FAILED!\n");

                //    // 强正则化重试
                //    print2Log("  Retrying with stronger regularization...\n");
                //    for (int ii = 0; ii < n; ii++) {
                //        AtWA(ii, ii) += diag_median * 0.01;
                //    }

                //    ldlt.compute(AtWA);
                //    if (ldlt.info() != Eigen::Success) {
                //        print2Log("ERROR: Decomposition still failed!\n");
                //        break;
                //    }
                //}

                x = ldlt.solve(AtWb);

                if (ldlt.info() != Eigen::Success) {
                    print2Log("FAILED!\n");
                    break;
                }

                print2Log("Done\n");
                // ===== 检查解的合理性 =====
                double x_min = x.minCoeff();
                double x_max = x.maxCoeff();
                int nan_count = 0, inf_count = 0, large_count = 0;

                for (int ii = 0; ii < n; ii++) {
                    if (std::isnan(x(ii))) nan_count++;
                    else if (std::isinf(x(ii))) inf_count++;
                    else if (fabs(x(ii)) > 1e6) large_count++;
                }

                print2Log("  Solution range: [%.4f, %.4f]\n", x_min, x_max);
                if (nan_count > 0) print2Log("  ERROR: %d NaN values!\n", nan_count);
                if (inf_count > 0) print2Log("  ERROR: %d Inf values!\n", inf_count);
                if (large_count > 0) print2Log("  WARNING: %d abnormally large values (>1e6)!\n", large_count);

                // ===== 计算残差 =====
                print2Log("  Computing residuals...");

                std::vector<double> abs_residuals;
                abs_residuals.reserve(totalObs);

                double vv_sum = 0.0;
                int valid_count = 0;
                global_obs_id = 0;

                for (int fi = 0; fi < numOlpFiles; fi++) {
                    const OlpInfo& info = olpList[fi];
                    COlpFile olpF; if (!olpF.Load4File(info.path)) continue;
                    int oz; OBV* pOs = olpF.GetData(&oz);
                    int idx = info.idx, idxr = info.idxr;
                    int oidBase = fileOidStart[fi];

                    for (int vi = 0; vi < oz; vi++, pOs++) {
                        int oid = oidBase + vi;
                        double w = (oid < (int)obsWeights.size()) ? obsWeights[oid] : 0.0;

                        double k1 = getKval(1, pOs->csz, pOs->cvz, pOs->cas)/* - pAK1[idx];*/;
                        double k2 = getKval(4, pOs->csz, pOs->cvz, pOs->cas)/* - pAK2[idx];*/;
                        double cv = pOs->cv[c]/* / DN_SCALE*/;
                        double rv = pOs->rv[c]/* / DN_SCALE*/;

                        double k1r, k2r;
                        if (idxr == -1) {
                            k1r = 0;
                            k2r = 0;
                        }
                        else {
                            k1r = getKval(1, pOs->rsz, pOs->rvz, pOs->ras)/* - pAK1[idxr]*/;
                            k2r = getKval(4, pOs->rsz, pOs->rvz, pOs->ras) /*- pAK2[idxr]*/;
                        }

                        if (w >= 0.001) {
                            double residual;
                            if (idxr == -1) {
                                double computed = -x(idx * 4) - k1 * x(idx * 4 + 1) - k2 * x(idx * 4 + 2) + cv * x(idx * 4 + 3);
                                residual = computed - rv;
                            }
                            else {
                                double val_idx = x(idx * 4) + k1 * x(idx * 4 + 1) + k2 * x(idx * 4 + 2) - cv/* * x(idx * 4 + 3)*/;
                                double val_idxr = x(idxr * 4) + k1r * x(idxr * 4 + 1) + k2r * x(idxr * 4 + 2) - rv/* * x(idxr * 4 + 3)*/;
                                residual = val_idx - val_idxr;
                            }

                            vv_sum += residual * residual * w;
                            valid_count++;
                            abs_residuals.push_back(fabs(residual));
                        }

                        global_obs_id++;

                    }
                }

                std::sort(abs_residuals.begin(), abs_residuals.end());
                sigma0 = abs_residuals[abs_residuals.size() / 2] * 1.4826;
                double rmse = sqrt(vv_sum / valid_count);

                print2Log("Done\n");
                print2Log("  Statistics (normalized): sigma0=%.4f, RMSE=%.4f\n", sigma0, rmse);
                //print2Log("  Statistics (DN units): sigma0=%.1f, RMSE=%.1f\n",
                //    sigma0* DN_SCALE, rmse* DN_SCALE);
                char residual_json[512];
                sprintf(residual_json, "%sresiduals_band%d_iter%d.json", strRom, c + 1, iter + 1);
                ExportResidualsToJSON(residual_json, olpList, obsWeights, fileOidStart,
                    numOlpFiles, c, iter + 1);
                if (iter == max_iterations - 1) {
                    break;
                }

                // ===== 更新权重 =====
                print2Log("  Updating weights...");

                int outlier_count = 0;
                for (size_t oid = 0; oid < abs_residuals.size(); oid++) {
                    double std_res = abs_residuals[oid] / (sigma0 + 1e-10);
                    double new_w = IGG3Weight(std_res, initWeights[oid]);

                    if (new_w < obsWeights[oid] * 0.8) {
                        outlier_count++;
                    }

                    obsWeights[oid] = new_w;
                }

                print2Log("Done (outliers=%d, %.1f%%)\n",
                    outlier_count, 100.0 * outlier_count / abs_residuals.size());
                print2Log("  Time: %.2fs\n", (GetTickCount() - iter_st) * 0.001);

                ProgStep(cancel);
            }

            for (auto& f : excellentFiles) free((void*)f);
            for (auto& f : goodFiles) free((void*)f);
            for (auto& f : fairFiles) free((void*)f);
            for (auto& f : poorFiles) free((void*)f);

            // ===== 输出结果 =====
            sprintf(str, "%s//pre_bnd%d.txt", strRom, c + 1);
            FILE* fKnl = fopen(str, "wt");
            if (fKnl) {
                for (i = 0; i < sum; i++) {
                    m_listCtrl.GetItemText(i, 0, str, 256);
                    fprintf(fKnl, "%9.6lf \t %15.6lf \t %15.6lf \t %15.6lf \t %s\n",
                        x(i * 4 + 3), x(i * 4), x(i * 4 + 1), x(i * 4 + 2),
                        strrchr(str, '\\'));
                }
                fclose(fKnl);
            }

            print2Log("Band %d done. Total: %.2fs\n\n",
                c + 1, (GetTickCount() - st) * 0.001);
        }

        for (i = 0; i < sum; i++) delete[] tskFileList[i];
        delete[] tskFileList;
        delete[] pAK1;
        //delete[]pAK1;

        ProgEnd();
        print2Log("RadBA completed.\n");
    }

   	if (m_hW4EndHdl) ::SetEvent( m_hW4EndHdl );
    if (m_hEThEHdl ) ::CloseHandle(m_hEThEHdl); m_hEThEHdl = NULL;
	::CloseHandle( m_hThread ); m_hThread = NULL;
    
    PostMessage( WM_OUTPUT_MSG,THREADEND,0 );
}

void CRadCaliDlg::OnTaskOver( UINT tskId )
{
    int tskSum = m_listCtrl.GetItemCount();
    for ( int i=0;i<tskSum;i++ ){
        if ( int(tskId)==atoi(m_listCtrl.GetItemText(i,TSK_ID)) ){
            m_listCtrl.SetItemText( i,TSK_STA,TSK_STA_OVER );
        }
    } 
}

void CRadCaliDlg::OnTaskTerm( UINT tskGrpId,UINT tskId )
{
    int tskSum = m_listCtrl.GetItemCount();
    for ( int i=0;i<tskSum;i++ ){
        if ( int(tskGrpId)==atoi(m_listCtrl.GetItemText(i,TSK_GRP)) && 
            int(tskId)==atoi(m_listCtrl.GetItemText(i,TSK_ID)) ){
            m_listCtrl.SetItemText( i,TSK_STA,TSK_STA_TERM );
        }
    }       
}

void CRadCaliDlg::OnTaskExit( UINT tskGrpId,UINT tskId )
{
    int tskSum = m_listCtrl.GetItemCount();
    for ( int i=0;i<tskSum;i++ ){
        if ( int(tskGrpId)==atoi(m_listCtrl.GetItemText(i,TSK_GRP)) && 
            int(tskId)==atoi(m_listCtrl.GetItemText(i,TSK_ID)) ){
            
            cprintf( "\n\n========================== %s\n",m_listCtrl.GetItemText(i,TSK_STA) );
            if ( m_listCtrl.GetItemText(i,TSK_STA)!=TSK_STA_OVER )            
                m_listCtrl.SetItemText( i,TSK_STA,TSK_STA_EXIT );
        }
    }               
}



/*======================================================================
 *  TIFF 影像仿射变换封装与匹配实现
 *
 *  坐标约定（GDAL GeoTransform 6参数，像元左上角坐标）：
 *    gt[0]  = 左上角像元左上角 X（经度或 UTM Easting）
 *    gt[1]  = 每列的 X 增量（正数）
 *    gt[2]  = 行旋转项（通常为 0）
 *    gt[3]  = 左上角像元左上角 Y（纬度或 UTM Northing）
 *    gt[4]  = 列旋转项（通常为 0）
 *    gt[5]  = 每行的 Y 增量（正北向影像为负数）
 *
 *  像元中心坐标（与原 tfw 约定一致，因此公式等价）：
 *    X(col,row) = (gt[0]+gt[1]*0.5) + gt[1]*col + gt[2]*row
 *    Y(col,row) = (gt[3]+gt[5]*0.5) + gt[4]*col + gt[5]*row
 *  即 Ex = gt[0]+gt[1]*0.5，Dx = gt[1]，Ry = gt[2]
 *     Ny = gt[3]+gt[5]*0.5，Rx = gt[4]，Dy = gt[5]
 *
 *  逆变换（地理→像素中心）：
 *    det = Dx*Dy - Ry*Rx
 *    col = ( Dy*(X-Ex) - Ry*(Y-Ny) ) / det
 *    row = ( Dx*(Y-Ny) - Rx*(X-Ex) ) / det
 *
 *  内存布局：BIP，uint16，行 0 = 影像顶行，与 GDAL RasterIO 一致。
 *  OLP 坐标：直接存顶→下像素坐标，row=0 = 影像顶行，不翻转。
 *======================================================================*/


/*======================================================================
 *  CLasHeightGrid
 *
 *  从一组未分类 LAS/LAZ 文件构建三个格网并提供逐点查询：
 *
 *    ZMax    ── 每个格网单元内最高回波Z（近似DSM）
 *    ZMin    ── 每个格网单元内最低回波Z（原始，含噪声）
 *    ZGnd    ── 局部地面高程估计：对 ZMin 做半径R的移动窗口最小值滤波
 *               （简化布料滤波，无需分类，等价于"去掉高出地面的点"）
 *    Density ── 点密度（点/格网单元），密度骤降区域为遮挡/阴影
 *
 *  地面判据（IsGround）：
 *    nDSM = ZMax - ZGnd < hThresh  →  地面附近，允许匹配
 *    Density > densMin              →  有足够点才信任判断
 *
 *  GetZ 返回 ZGnd（地面高程），用于 getSunPos 的高程参数，
 *  避免把建筑顶部高程代入导致投影差计算错误。
 *
 *  读取方式：纯 C++ 解析 LAS 1.0-1.4 二进制，无额外依赖。
 *======================================================================*/
struct CLasHeightGrid
{
    double  Ex, Ny;      // 格网左上角像元中心地理坐标
    double  gsd;         // 格网分辨率（米）
    int     cols, rows;  // 格网大小
    float* pZMax;       // DSM：最高回波Z
    float* pZMin;       // 原始最低Z（含噪声）
    float* pZGnd;       // 地面高程估计（ZMin 移动窗口最小值滤波后）
    WORD* pDens;       // 点密度（点数，上限65535）
    static const float no_data;

    CLasHeightGrid()
        : Ex(0), Ny(0), gsd(1), cols(0), rows(0),
        pZMax(NULL), pZMin(NULL), pZGnd(NULL), pDens(NULL) {
    }
    ~CLasHeightGrid() { Free(); }

    void Free() {
        delete[] pZMax; pZMax = NULL;
        delete[] pZMin; pZMin = NULL;
        delete[] pZGnd; pZGnd = NULL;
        delete[] pDens; pDens = NULL;
        cols = rows = 0;
    }

    // 加载目录下所有 .las/.laz
    // gsdM       ：格网分辨率（米），0 则取 domImg.GSD()*2
    // gndRadCells：地面估计的移动窗口半径（格网单元数），典型值 5~15
    //              对应的实地半径 = gsdM * gndRadCells，应 > 最高建筑宽度一半
            // 1. 读LAS文件头获取实际XY范围（用于格网范围计算）
        //    LAS 1.x Public Header Block（正确字段大小）：
        //    offset  94: Header Size    = UINT16 (2B)  ← 原代码此处错误地读了4字节！
        //    offset  96: Pt Data Offset = UINT32 (4B)
        //    offset 100: Num VLRs       = UINT32 (4B)
        //    offset 104: Pt Format ID   = UINT8  (1B)
        //    offset 105: Pt Rec Length  = UINT16 (2B)
        //    offset 107: Num Pt Records = UINT32 (4B)
        //    offset 179: Max X          = double (8B)
        //    offset 187: Min X          = double (8B)
        //    offset 195: Max Y          = double (8B)
        //    offset 203: Min Y          = double (8B)
    bool Load(const char* lasDir, double gsdM, const CTifAffine& domImg,
        int gndRadCells = 10)
    {
        Free();
        if (!lasDir || !lasDir[0]) return false;

        gsd = (gsdM > 0) ? gsdM : domImg.GSD() * 2.0;
        if (gsd < 0.3) gsd = 0.3;

        Ex = domImg.Ex;
        Ny = domImg.Ny;
        cols = int(std::ceil(domImg.cols * fabs(domImg.Dx) / gsd)) + 4;
        rows = int(std::ceil(domImg.rows * fabs(domImg.Dy) / gsd)) + 4;

        LONGLONG sz = LONGLONG(rows) * cols;
        pZMax = new float[sz]; std::fill(pZMax, pZMax + sz, no_data);
        pZMin = new float[sz]; std::fill(pZMin, pZMin + sz, no_data);
        pZGnd = new float[sz]; std::fill(pZGnd, pZGnd + sz, no_data);
        pDens = new WORD[sz];  memset(pDens, 0, sz * sizeof(WORD));

        // ── 枚举并读取所有 LAS/LAZ ──
        int nFiles = 0;
        LONGLONG nPts = 0;
        char pattern[512]; sprintf(pattern, "%s\\*", lasDir);
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(pattern, &fd);
        if (hFind == INVALID_HANDLE_VALUE) {
            cprintf("[CLasHeightGrid] cannot open dir: %s\n", lasDir);
            return false;
        }
        do {
            const char* nm = fd.cFileName;
            int L = (int)strlen(nm);
            if (L <= 4) continue;
            if (_stricmp(nm + L - 4, ".las") != 0 && _stricmp(nm + L - 4, ".laz") != 0) continue;
            char fpath[512]; sprintf(fpath, "%s\\%s", lasDir, nm);
            LONGLONG n = _LoadOneLas(fpath);
            if (n > 0) { nFiles++; nPts += n; }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);

        if (nFiles == 0) {
            cprintf("[CLasHeightGrid] no LAS files found in: %s\n", lasDir);
            return false;
        }
        cprintf("[CLasHeightGrid] %d files, %lld pts → grid %d×%d gsd=%.2fm\n",
            nFiles, nPts, cols, rows, gsd);

        // ── 地面高程估计：对 ZMin 做移动窗口最小值（半径 gndRadCells）──
        // 这是简化的布料滤波：在足够大的邻域内，最低点必然是地面
        // 复杂度 O(rows*cols*R²) 对于典型参数（R=10, 2000×2000格网）约 4亿次，
        // 用行列分离（两次1D最小值滤波）降至 O(rows*cols*R)
        //_MinFilter1D(pZMin, pZGnd, gndRadCells);
        //cprintf("[CLasHeightGrid] ground estimation done (radius=%d cells = %.1fm)\n",
        //    gndRadCells, gndRadCells * gsd);

        return true;
    }

    // 查询 (gx,gy) 是否为地面（可匹配）
    // hThresh  ：nDSM 阈值（米），建议 1.5~2.5
    // minDensity：最低点密度（点/格网单元），低于此值认为是遮挡/阴影，不可信
    bool IsGround(double gx, double gy,
        double hThresh = 2.0, int minDensity = 2) const
    {
        int c, r; if (!_GeoToGrid(gx, gy, c, r)) return true;
        LONGLONG gi = LONGLONG(r) * cols + c;

        // 密度过低：点云覆盖稀疏或被遮挡，不信任，跳过
        if (pDens[gi] < minDensity) return false;

        float zmax = pZMax[gi], zgnd = pZGnd[gi];
        if (zmax == no_data || zgnd == no_data) return true; // 无数据不过滤

        // nDSM = ZMax - ZGnd：地物高于地面的高度
        return (zmax - zgnd) < float(hThresh);
    }

    // 查询 (gx,gy) 的地面高程 ZGnd（米）
    // 用于 getSunPos 的高程参数（优于固定 grdZ）
    bool GetZ(double gx, double gy, double& z) const {
        int c, r; if (!_GeoToGrid(gx, gy, c, r)) return false;
        float v = pZMax[LONGLONG(r) * cols + c];
        if (v == no_data) return false;
        z = double(v); return true;
    }

private:
    bool _GeoToGrid(double gx, double gy, int& c, int& r) const {
        if (!pZMax) return false;
        c = int((gx - Ex) / gsd + 0.5);
        r = int((Ny - gy) / gsd + 0.5);
        return (c >= 0 && r >= 0 && c < cols && r < rows);
    }

    // 读取一个 LAS 文件，更新 pZMax/pZMin/pDens
    LONGLONG _LoadOneLas(const char* fpath)
    {
        FILE* f = fopen(fpath, "rb");
        if (!f) return 0;

        // ── LAS 文件头解析 ──
        char sig[4] = {}; fread(sig, 1, 4, f);
        if (memcmp(sig, "LASF", 4) != 0) { fclose(f); return 0; }

        BYTE verMaj = 0, verMin = 0;
        fseek(f, 24, SEEK_SET);
        fread(&verMaj, 1, 1, f); fread(&verMin, 1, 1, f);

        fseek(f, 94, SEEK_SET);
        // ★ 修复：LAS Header Size 是 UINT16(2字节)，不是 DWORD(4字节)
        // 原代码用 DWORD 读导致后续 ptOffset/ptFmt/ptRecLen 全部错位
        WORD  headerSize = 0; fread(&headerSize, 2, 1, f);  // offset 94: UINT16
        DWORD ptOffset = 0;   fread(&ptOffset, 4, 1, f);  // offset 96: UINT32
        DWORD numVLR = 0;     fread(&numVLR, 4, 1, f);  // offset 100: UINT32

        BYTE  ptFmt = 0;    fread(&ptFmt, 1, 1, f);
        WORD  ptRecLen = 0; fread(&ptRecLen, 2, 1, f);
        DWORD nPtsLeg = 0;  fread(&nPtsLeg, 4, 1, f);

        fseek(f, 131, SEEK_SET);
        double scX, scY, scZ, ofX, ofY, ofZ;
        fread(&scX, 8, 1, f); fread(&scY, 8, 1, f); fread(&scZ, 8, 1, f);
        fread(&ofX, 8, 1, f); fread(&ofY, 8, 1, f); fread(&ofZ, 8, 1, f);

        LONGLONG nPts = nPtsLeg;
        if (verMin >= 4 && nPtsLeg == 0) {
            fseek(f, 247, SEEK_SET);
            fread(&nPts, 8, 1, f);
        }
        if (ptRecLen == 0 || nPts <= 0) { fclose(f); return 0; }

        // 强度字段偏移（Point Format 0-5: bytes 12-13; 6-10: bytes 12-13，一致）
        // classification 偏移：Format 0-5: byte 15; Format 6+: byte 16
        // 对于未分类点云我们不需要 classification，只用 XYZ + Intensity
        // Intensity 在所有格式中均为偏移12的 UINT16
        const int OFF_INTENSITY = 12;  // UINT16

        fseek(f, (long)ptOffset, SEEK_SET);

        const int BATCH = 65536;
        std::vector<BYTE> buf((size_t)BATCH * ptRecLen);
        LONGLONG remaining = nPts, loaded = 0;

        while (remaining > 0) {
            int batch = (int)(std::min)((LONGLONG)BATCH, remaining);
            int nRead = (int)fread(buf.data(), ptRecLen, batch, f);
            if (nRead <= 0) break;

            for (int i = 0; i < nRead; i++) {
                const BYTE* p = buf.data() + (size_t)i * ptRecLen;

                INT32 ix = *reinterpret_cast<const INT32*>(p);
                INT32 iy = *reinterpret_cast<const INT32*>(p + 4);
                INT32 iz = *reinterpret_cast<const INT32*>(p + 8);

                double X = ofX + scX * ix;
                double Y = ofY + scY * iy;
                float  Z = float(ofZ + scZ * iz);

                int c, r;
                if (!_GeoToGrid(X, Y, c, r)) continue;
                LONGLONG gi = LONGLONG(r) * cols + c;

                if (pZMax[gi] == no_data || Z > pZMax[gi]) pZMax[gi] = Z;
                if (pZMin[gi] == no_data || Z < pZMin[gi]) pZMin[gi] = Z;
                if (pDens[gi] < 65535) pDens[gi]++;
                loaded++;
            }
            remaining -= nRead;
        }
        fclose(f);
        return loaded;
    }

    // 行列分离的2D移动窗口最小值滤波
    // src → dst，半径 rad（格网单元）
    // 复杂度 O(N)（van Herk 算法，与半径无关）
    void _MinFilter1D(const float* src, float* dst, int rad)
    {
        LONGLONG sz = LONGLONG(rows) * cols;
        // 先行方向滤波 src → tmp
        std::vector<float> tmp(sz, no_data);

        // 行方向（水平）最小值
        for (int r = 0; r < rows; r++) {
            const float* row = src + (LONGLONG)r * cols;
            float* out = tmp.data() + (LONGLONG)r * cols;
            for (int c = 0; c < cols; c++) {
                float mn = no_data;
                int c0 = (std::max)(0, c - rad), c1 = (std::min)(cols - 1, c + rad);
                for (int k = c0; k <= c1; k++) {
                    float v = row[k];
                    if (v != no_data && (mn == no_data || v < mn)) mn = v;
                }
                out[c] = mn;
            }
        }
        // 列方向（垂直）最小值 tmp → dst
        for (int c = 0; c < cols; c++) {
            for (int r = 0; r < rows; r++) {
                float mn = no_data;
                int r0 = (std::max)(0, r - rad), r1 = (std::min)(rows - 1, r + rad);
                for (int k = r0; k <= r1; k++) {
                    float v = tmp[(LONGLONG)k * cols + c];
                    if (v != no_data && (mn == no_data || v < mn)) mn = v;
                }
                dst[(LONGLONG)r * cols + c] = mn;
            }
        }
    }
};
const float CLasHeightGrid::no_data = -9999.0f;

/*----------------------------------------------------------------------
 *  灰度提取（前3波段均值，用于 ZNCC）
 *----------------------------------------------------------------------*/
static inline double Gray3(const WORD* px, int bands) {
    int s = 0, v = 0, n = bands < 3 ? bands : 3;
    for (int b = 0; b < n; b++) if (px[b] > 0) { s += px[b]; v++; }
    return v ? double(s) / v : 0.0;
}

/*----------------------------------------------------------------------
 *  ZNCC 验证
 *  baseImg：用于划定窗口的参考影像（通常是较粗的那幅）
 *  hrImg  ：对应采样的另一幅
 *  cxBase, cyBase：窗口中心（顶→下坐标）
 *----------------------------------------------------------------------*/
static bool VerifyByZNCC(const CTifAffine& baseImg, const CTifAffine& hrImg,
    int cxBase, int cyBase, int rad, double znccThr)
{
    if (cxBase - rad < 0 || cyBase - rad < 0 || cxBase + rad >= baseImg.cols || cyBase + rad >= baseImg.rows) return false;

    // 细影像块平均窗口大小（分辨率比约为 baseGSD/hrGSD）
    int avz = int(std::round(baseImg.GSD() / hrImg.GSD()));
    if (avz < 1) avz = 1;
    if (avz % 2 == 0) avz++;

    const int side = 2 * rad + 1;
    std::vector<double> vB, vH; vB.reserve(side * side); vH.reserve(side * side);
    double sB = 0, sB2 = 0, sH = 0, sH2 = 0; int valid = 0;
    WORD tmp[16] = {};

    for (int dy = -rad; dy <= rad; dy++) {
        for (int dx = -rad; dx <= rad; dx++) {
            int xB = cxBase + dx, yB = cyBase + dy;
            double gB = Gray3(baseImg.PixPtr(xB, yB), baseImg.bands);
            vB.push_back(gB);
            double gx, gy; baseImg.Pix2Geo(xB, yB, gx, gy);
            double fc, fr; hrImg.Geo2Pix(gx, gy, fc, fr);
            double gH = 0;
            if (hrImg.Sample(fc, fr, avz >= 3 ? avz : 0, tmp)) gH = Gray3(tmp, hrImg.bands);
            vH.push_back(gH);
            if (gB > 0 && gH > 0) { sB += gB; sB2 += gB * gB; sH += gH; sH2 += gH * gH; valid++; }
        }
    }
    if (valid < side * side * 7 / 10) return false;
    double mB = sB / valid, mH = sH / valid;
    double stdB = (valid > 1 && sB2 - sB * sB / valid > 0) ? sqrt((sB2 - sB * sB / valid) / (valid - 1)) : 0;
    double stdH = (valid > 1 && sH2 - sH * sH / valid > 0) ? sqrt((sH2 - sH * sH / valid) / (valid - 1)) : 0;
    if (stdB < RAD_MIN_STD || stdH < RAD_MIN_STD) return false;
    double zncc = 0;
    for (int i = 0; i < (int)vB.size(); i++) zncc += (vB[i] - mB) * (vH[i] - mH);
    zncc /= (double(vB.size()) * stdB * stdH);
    return zncc >= znccThr;
}

/*----------------------------------------------------------------------
 *  绘制匹配连线图并保存 PNG（纯 GDAL 实现，不依赖 OpenCV）
 *
 *  策略：
 *    - 两幅影像各自按地理坐标重投影为正北向灰度图（同一 outGsd）
 *    - 行方向用统一的 Yhi_all 对齐，使同一地理 Y 在两幅子图中行号相同
 *    - 左右拼接，连线的 cLy == cRy（同名点地理 Y 相同时连线水平）
 *    - 用 Bresenham 直线 + 实心圆点；GDAL MEM→PNG 驱动写出
 *----------------------------------------------------------------------*/
static void SaveMatchPlot(const CTifAffine& domImg, const CTifAffine& refImg,
    COlpFile& olp, const char* outPath)
{
    int oz = 0; const OBV* pData = olp.GetData(&oz);
    if (!pData || oz <= 0) return;

    // ── 1. 地理包围盒（直接调 GetBBox）──
    double dXlo, dXhi, dYlo, dYhi, rXlo, rXhi, rYlo, rYhi;
    domImg.GetBBox(dXlo, dXhi, dYlo, dYhi);
    refImg.GetBBox(rXlo, rXhi, rYlo, rYhi);

    // ── 2. 输出分辨率 & 统一北边界 ──
    double outGsd = (std::min)(domImg.GSD(), refImg.GSD());
    const int MAX_DIM = 2048;
    double maxSpan = (std::max)({ dXhi - dXlo, dYhi - dYlo, rXhi - rXlo, rYhi - rYlo });
    if (maxSpan / outGsd > MAX_DIM) outGsd = maxSpan / MAX_DIM;
    double Yhi_all = (std::max)(dYhi, rYhi);

    int WL = (std::max)(1, int((dXhi - dXlo) / outGsd + 0.5));
    int HL = (std::max)(1, int((dYhi - dYlo) / outGsd + 0.5));
    int WR = (std::max)(1, int((rXhi - rXlo) / outGsd + 0.5));
    int HR = (std::max)(1, int((rYhi - rYlo) / outGsd + 0.5));
    int offDrow = int((Yhi_all - dYhi) / outGsd + 0.5);
    int offRrow = int((Yhi_all - rYhi) / outGsd + 0.5);
    const int GAP = 20;
    int canvH = (std::max)(offDrow + HL, offRrow + HR);
    int canvW = WL + GAP + WR;

    // ── 3. 渲染灰度图（2%~98% 分位拉伸）──
    auto MakeGray = [](const CTifAffine& img, double Xlo, double Yhi_,
        double gsd_, int W, int H) -> std::vector<uint8_t>
        {
            std::vector<uint8_t> out(W * H, 0); WORD tmp[16] = {};
            std::vector<float> vals; vals.reserve(W * H / 16);
            for (int gy = 0; gy < H; gy += 4) {
                double gY = Yhi_ - (gy + 0.5) * gsd_;
                for (int gx = 0; gx < W; gx += 4) {
                    double gX = Xlo + (gx + 0.5) * gsd_;
                    double fc, fr; img.Geo2Pix(gX, gY, fc, fr);
                    if (fc >= 1 && fr >= 1 && fc < img.cols - 2 && fr < img.rows - 2)
                        if (img.BilinearSample(fc, fr, tmp)) {
                            float g = float(Gray3(tmp, img.bands)); if (g > 0)vals.push_back(g);
                        }
                }
            }
            float lo = 0, hi = 4096;
            if (vals.size() > 100) {
                std::sort(vals.begin(), vals.end());
                lo = vals[vals.size() * 2 / 100]; hi = vals[vals.size() * 98 / 100];
                if (hi <= lo + 1) hi = lo + 256;
            }
            for (int gy = 0; gy < H; gy++) {
                double gY = Yhi_ - (gy + 0.5) * gsd_;
                for (int gx = 0; gx < W; gx++) {
                    double gX = Xlo + (gx + 0.5) * gsd_;
                    double fc, fr; img.Geo2Pix(gX, gY, fc, fr);
                    if (fc >= 0 && fr >= 0 && fc < img.cols - 1 && fr < img.rows - 1)
                        if (img.BilinearSample(fc, fr, tmp)) {
                            float g = float(Gray3(tmp, img.bands));
                            int v = int((g - lo) * 255.0f / (hi - lo) + 0.5f);
                            out[gy * W + gx] = uint8_t(v < 0 ? 0 : v>255 ? 255 : v);
                        }
                }
            }
            return out;
        };
    auto gL = MakeGray(domImg, dXlo, dYhi, outGsd, WL, HL);
    auto gR = MakeGray(refImg, rXlo, rYhi, outGsd, WR, HR);

    // ── 4. 拼接到 BGR 画布 ──
    std::vector<uint8_t> canvas(canvH * canvW * 3, 0);
    auto pix = [&](int x, int y)->uint8_t* { return canvas.data() + (y * canvW + x) * 3; };
    for (int r = 0; r < HL; r++) for (int c = 0; c < WL; c++) {
        uint8_t* p = pix(c, offDrow + r); p[0] = p[1] = p[2] = gL[r * WL + c];
    }
    for (int r = 0; r < HR; r++) for (int c = 0; c < WR; c++) {
        uint8_t* p = pix(WL + GAP + c, offRrow + r); p[0] = p[1] = p[2] = gR[r * WR + c];
    }
    // 分隔线
    for (int r = 0; r < canvH; r++) { uint8_t* p = pix(WL + GAP / 2, r); p[0] = p[1] = p[2] = 60; }

    // ── 5. 绘制连线 ──
    auto hashCol = [](int i, uint8_t& R, uint8_t& G, uint8_t& B) {
        unsigned h = (unsigned)(i * 2654435761u);
        R = uint8_t(80 + (h & 0x7F)); h >>= 7;
        G = uint8_t(80 + (h & 0x7F)); h >>= 7;
        B = uint8_t(80 + (h & 0x7F)); };
    auto dLine = [&](int x0, int y0, int x1_, int y1_, uint8_t R, uint8_t G, uint8_t B) {
        int dx = abs(x1_ - x0), dy = abs(y1_ - y0), sx = x0 < x1_ ? 1 : -1, sy = y0 < y1_ ? 1 : -1, err = dx - dy;
        while (true) {
            if (x0 >= 0 && x0 < canvW && y0 >= 0 && y0 < canvH) {
                uint8_t* p = pix(x0, y0); p[0] = B; p[1] = G; p[2] = R;
            }
            if (x0 == x1_ && y0 == y1_)break; int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x0 += sx; }if (e2 < dx) { err += dx; y0 += sy; }
        } };
    auto dDot = [&](int cx, int cy, uint8_t R, uint8_t G, uint8_t B) {
        for (int dy_ = -4; dy_ <= 4; dy_++) for (int dx_ = -4; dx_ <= 4; dx_++)
            if (dx_ * dx_ + dy_ * dy_ <= 16) {
                int x = cx + dx_, y = cy + dy_;
                if (x >= 0 && x < canvW && y >= 0 && y < canvH) {
                    uint8_t* p = pix(x, y); p[0] = B; p[1] = G; p[2] = R;
                }
            } };

    int step = (std::max)(1, oz / 500);
    for (int i = 0; i < oz; i += step) {
        const OBV& o = pData[i]; uint8_t R, G, B; hashCol(i / step, R, G, B);
        double gX, gY;
        domImg.Pix2Geo(o.cc, o.cr, gX, gY);
        int cLx = int((gX - dXlo) / outGsd + 0.5);
        int cLy = offDrow + int((dYhi - gY) / outGsd + 0.5);
        refImg.Pix2Geo(o.rc, o.rr, gX, gY);
        int cRx = WL + GAP + int((gX - rXlo) / outGsd + 0.5);
        int cRy = offRrow + int((rYhi - gY) / outGsd + 0.5);
        if (cLx < 0 || cLy < 0 || cLx >= WL || cLy >= canvH) continue;
        if (cRx < WL + GAP || cRy < 0 || cRx >= canvW || cRy >= canvH) continue;
        dLine(cLx, cLy, cRx, cRy, R, G, B);
        dDot(cLx, cLy, R, G, B); dDot(cRx, cRy, R, G, B);
    }

    // ── 6. GDAL MEM→PNG 写出 ──
    GDALDriver* pngDrv = GetGDALDriverManager()->GetDriverByName("PNG");
    GDALDriver* memDrv = GetGDALDriverManager()->GetDriverByName("MEM");
    if (!pngDrv || !memDrv) { cprintf("[SaveMatchPlot] PNG/MEM driver unavailable\n"); return; }
    GDALDataset* mem = memDrv->Create("", canvW, canvH, 3, GDT_Byte, NULL);
    if (!mem) return;
    std::vector<uint8_t> band(canvW * canvH);
    for (int ch = 0; ch < 3; ch++) {
        int off = 2 - ch; // BGR→RGB
        for (int k = 0; k < canvW * canvH; k++) band[k] = canvas[k * 3 + off];
        mem->GetRasterBand(ch + 1)->RasterIO(GF_Write, 0, 0, canvW, canvH,
            band.data(), canvW, canvH, GDT_Byte, 0, 0);
    }
    GDALDataset* png = pngDrv->CreateCopy(outPath, mem, 0, NULL, NULL, NULL);
    if (png) { GDALClose(png); cprintf("[SaveMatchPlot] saved: %s\n", outPath); }
    else     cprintf("[SaveMatchPlot] PNG write failed: %s\n", outPath);
    GDALClose(mem);
}

enum TiePointQuality {
    QUALITY_EXCELLENT = 0,  // 优秀
    QUALITY_GOOD = 1,       // 良好
    QUALITY_FAIR = 2,       // 一般
    QUALITY_POOR = 3,       // 较差
    QUALITY_BAD = 4         // 差
};

// isAbsolute=true : cv[]为航空DN，rv[]为哨兵DN（绝对约束配对）
// isAbsolute=false: cv[]和rv[]均为航空DN（相对约束配对）
static TiePointQuality EvaluateTiePointQuality(const OBV& pt, bool isAbsolute) {

    double meanCv = (pt.cv[0] + pt.cv[1] + pt.cv[2] + pt.cv[3]) / 4.0; // 航空侧
    double meanRv = (pt.rv[0] + pt.rv[1] + pt.rv[2] + pt.rv[3]) / 4.0; // 参考侧

    if (isAbsolute) {
        // NDVI：两端都在各自归一化空间内计算，比值是单位无关量
        double ndvi_aerial = 0.0, ndvi_sentinel = 0.0;
        float cv3 = pt.cv[3], cv2 = pt.cv[2];
        float rv3 = pt.rv[3], rv2 = pt.rv[2];
        if (cv3 + cv2 > 10.0f)
            ndvi_aerial = (cv3 - cv2) / (cv3 + cv2);
        if (rv3 + rv2 > 1.0f)
            ndvi_sentinel = (rv3 - rv2) / (rv3 + rv2);
        double ndviDiff = fabs(ndvi_aerial - ndvi_sentinel);

        // NDVI差异过大：地物类型不一致（可能时相差异或配准误差）
        if (ndviDiff > 0.35) return QUALITY_BAD;

        // 中等及以上亮度，NDVI一致性判等级
        if (ndviDiff < 0.10) {
            // 低NDVI（道路/裸土/建筑）：辐射约束最可靠
            if (fabs(ndvi_aerial) < 0.20 && fabs(ndvi_sentinel) < 0.20)
                return QUALITY_EXCELLENT;
            return QUALITY_GOOD;
        }
        if (ndviDiff < 0.20) return QUALITY_GOOD;
        return QUALITY_FAIR;
    }
    else {
        // ── 相对约束：cv和rv均为航空DN ──
        double ndvi1 = 0.0, ndvi2 = 0.0;
        if (pt.cv[3] + pt.cv[2] > 10.0f)
            ndvi1 = (pt.cv[3] - pt.cv[2]) / (pt.cv[3] + pt.cv[2]);
        if (pt.rv[3] + pt.rv[2] > 10.0f)
            ndvi2 = (pt.rv[3] - pt.rv[2]) / (pt.rv[3] + pt.rv[2]);
        double ndviDiff = fabs(ndvi1 - ndvi2);

        if (ndviDiff > 0.40) return QUALITY_BAD;

        if (ndviDiff < 0.10) {
            if (fabs(ndvi1) < 0.20 && fabs(ndvi2) < 0.20)
                return QUALITY_EXCELLENT;
            return QUALITY_GOOD;
        }
        if (ndviDiff < 0.20) return QUALITY_GOOD;
        if (ndvi1 > 0.30 || ndvi2 > 0.30) return QUALITY_FAIR;
        return QUALITY_FAIR;
    }
}

// 根据质量等级返回权重系数
static double GetQualityWeight(TiePointQuality quality) {
    switch (quality) {
    case QUALITY_EXCELLENT: return 1.0;   // 优秀
    case QUALITY_GOOD:      return 0.9;   // 良好
    case QUALITY_FAIR:      return 0.7;   // 一般
    case QUALITY_POOR:      return 0.4;   // 较差
    case QUALITY_BAD:       return 0.0;   // 差（过滤）
    default:                return 0.5;
    }
}
/*----------------------------------------------------------------------
 *  匹配点质量统计（每对影像）
 *  在MchTie末尾调用，将各质量等级计数写入 .olpstat 文本文件，
 *  供后续可视化或日志分析使用。
 *----------------------------------------------------------------------*/
static void SaveOlpQualityStat(
    const char* strOlpBase,     // 原始.olp路径（用于构造.olpstat路径）
    bool isAbsolute,            // 是否为绝对约束对（哨兵配对）
    int nExcellent, int nGood, int nFair, int nPoor, int nBad,
    double meanCv, double meanRv)
{
    char statPath[512];
    strcpy(statPath, strOlpBase);
    char* p = strrchr(statPath, '.'); if (p) strcpy(p, ".olpstat");
    FILE* f = fopen(statPath, "wt"); if (!f) return;
    int total = nExcellent + nGood + nFair + nPoor + nBad;
    fprintf(f, "type=%s\n", isAbsolute ? "absolute(sentinel)" : "relative(aerial)");
    fprintf(f, "total=%d excellent=%d good=%d fair=%d poor=%d bad=%d\n",
        total, nExcellent, nGood, nFair, nPoor, nBad);
    fprintf(f, "meanDN_src=%.1f meanDN_ref=%.1f\n", meanCv, meanRv);
    if (total > 0)
        fprintf(f, "excellent_pct=%.1f good_pct=%.1f fair_pct=%.1f poor_pct=%.1f bad_pct=%.1f\n",
            100.0 * nExcellent / total, 100.0 * nGood / total, 100.0 * nFair / total,
            100.0 * nPoor / total, 100.0 * nBad / total);
    fclose(f);
}
/*----------------------------------------------------------------------
 *  并行匹配结果记录
 *----------------------------------------------------------------------*/
struct MatchRec {
    int cxDom, cyDom;   // 主影像像素（顶→下）
    int cxRef, cyRef;   // 参考影像像素（顶→下）
    WORD cv[8], rv[8];  // 主/参考各波段（最多8波段）
    double sz, vz, as;
    double sz1, vz1, as1;
};

/////////////////////////////////////////////////////////////////////////////
BOOL MchTie(LPCSTR lpstrPar)
{
    char strSrc[512], strRef[512], strTsk[512], str[1024];
    char strOlp[512] = {}, strOlpExcellent[512] = {}, strOlpGood[512] = {},
        strOlpFair[512] = {}, strOlpPoor[512] = {};
    double cx, cy, cz, phi, img_, kap, grdZ;
    double cx1, cy1, cz1, phi1, img1, kap1, grdZ1;
    int idx, yy, mm, dd, ho, mi, se, yy1, mm1, dd1, ho1, mi1, se1;
    int utmZn = 0, gs = 21, ws = 5, bTxt = 0;

    // 解析任务文件路径
    const char* pS = strrchr(lpstrPar, '@');
    if (pS) { pS++; while (*pS == ' ') pS++; }
    else pS = lpstrPar;
    strcpy(strTsk, pS);

    FILE* fTsk = fopen(strTsk, "rt"); if (!fTsk) return FALSE;

    // 第1行：主影像路径
    fgets(str, sizeof(str), fTsk); sscanf(str, "%s", strSrc); DOS_PATH(strSrc);

    // 第2行：影像参数
    fgets(str, sizeof(str), fTsk);
    sscanf(str, "%d%lf%lf%lf%lf%lf%lf%lf%d%d%d%d%d%d%d%d%d%d",
        &idx, &cx, &cy, &cz, &phi, &img_, &kap, &grdZ,
        &yy, &mm, &dd, &ho, &mi, &se, &utmZn, &gs, &ws, &bTxt);
    int step = gs;
    // 第3行（可选）：LAS 点云目录路径（留空或省略则不加载）
    // 格式：LAS=<目录路径>  或直接写路径
    char strLasDir[512] = {};
    {
        long pos = ftell(fTsk);
        if (fgets(str, sizeof(str), fTsk)) {
            // 判断是否是参数行（以 LAS= 开头）还是参考影像路径
            char tmp[512] = {};
            sscanf(str, "%s", tmp);
            if (_strnicmp(tmp, "LAS=", 4) == 0) {
                strcpy(strLasDir, tmp + 4); DOS_PATH(strLasDir);
            }
            else if (_strnicmp(tmp, "LAS:", 4) == 0) {
                strcpy(strLasDir, tmp + 4); DOS_PATH(strLasDir);
            }
            else {
                // 不是LAS行，回退，让后续逻辑重新读
                fseek(fTsk, pos, SEEK_SET);
            }
        }
    }

    // 加载主影像（GDAL），以主影像坐标系为基准
    CTifAffine domImg;
    if (!domImg.Load(strSrc)) { fclose(fTsk); return FALSE; }

    // 主影像坐标系 WKT，作为所有参考影像重投影的目标坐标系
    const char* domSrsWkt = domImg.srsWkt.c_str();

    // ── 加载 LAS 点云格网（可选）──
    CLasHeightGrid lasGrid;
    bool bHasLas = false;
    if (strLasDir[0]) {
        // 格网分辨率取主影像GSD的2倍（平衡精度和内存）
        double lasGsd = domImg.GSD();
        if (lasGsd < 0.5) lasGsd = 0.5;
        // gndRadCells=10 → 实地半径 = lasGsd*10，应大于最高建筑宽度一半
        // 城区高楼密集时可增大到15~20
        bHasLas = lasGrid.Load(strLasDir, lasGsd, domImg, 10);
        if (bHasLas)
            cprintf("[MchTie] LAS grid loaded: gsd=%.2fm, will filter non-ground points\n", lasGsd);
        else
            cprintf("[MchTie] WARNING: LAS dir specified but load failed: %s\n", strLasDir);
    }
    CWuGeoCvt geoCvt;
    geoCvt.Set_Cvt_Par(ET_WGS84, UTM_PROJECTION, SEMIMAJOR_WGS84, SEMIMINOR_WGS84, 0, Zone2CenterMerdian(utmZn) * SPGC_D2R, 500000, 0, 0.9996, 0);

    // ── 计算 avK1 / avK2（遍历像元，步长 step）──
    {
        char strKM[512]; strcpy(strKM, strTsk); strcat(strKM, "_skm.txt");
        FILE* fKM = fopen(strKM, "wt");
        if (fKM) {
            double avK1 = 0, avK2 = 0, ks = 0;
            double sz, vz, as, gx, gy,gz;
            for (int r = 1; r < domImg.rows - step; r += step) {
                for (int c = 1; c < domImg.cols - step; c += step) {
                    if (CTifAffine::IsBlack(domImg.PixPtr(c, r), domImg.bands)) continue;
                    domImg.Pix2Geo(c, r, gx, gy);
                    double ptZ0 = grdZ; if (bHasLas) lasGrid.GetZ(gx, gy, ptZ0);
                    double lon, lat, hei;
                    gz = ptZ0;
                    geoCvt.Cvt_Prj2LBH(gx, gy, gz, &lon, &lat, &hei);
                    getSunPos(gx, gy, gz, cx, cy, cz, lon * SPGC_R2D, lat * SPGC_R2D, yy, mm, dd, ho, mi, se, &sz, &vz, &as);
                    avK1 += getKval(1, sz, vz, as); avK2 += getKval(4, sz, vz, as); ks += 1;
                }
            }
            if (ks > 0) { avK1 /= ks; avK2 /= ks; }
            cprintf("avK1=%lf avK2=%lf\n", avK1, avK2);
            fprintf(fKM, "%lf %lf\n", avK1, avK2); fclose(fKM);
        }
    }

    // ── 逐参考影像匹配 ──
    COlpFile olpF;
    COlpFile olpExcellent, olpGood, olpFair, olpPoor;
    while (!feof(fTsk)) {
        if (!fgets(str, sizeof(str), fTsk)) break;
        sscanf(str, "%s", strRef); DOS_PATH(strRef);
        if (!fgets(str, sizeof(str), fTsk)) break;
        if (sscanf(str, "%d%lf%lf%lf%lf%lf%lf%lf%d%d%d%d%d%d",
            &idx, &cx1, &cy1, &cz1, &phi1, &img1, &kap1, &grdZ1,
            &yy1, &mm1, &dd1, &ho1, &mi1, &se1) < 10) break;
        if (idx == -1)step = 1; else step = gs;
        // OLP 路径
        strcpy(strOlp, strTsk); strcpy(strrchr(strOlp, '.'), "_");
        strcat(strOlp, strrchr(strRef, '\\') + 1); strcat(strOlp, ".olp");
        cprintf("%s\n", strOlp);
        strcpy(strOlpExcellent, strOlp); char* p = strrchr(strOlpExcellent, '.'); if (p) strcpy(p, "_excellent.olp");
        strcpy(strOlpGood, strOlp); p = strrchr(strOlpGood, '.'); if (p) strcpy(p, "_good.olp");
        strcpy(strOlpFair, strOlp); p = strrchr(strOlpFair, '.'); if (p) strcpy(p, "_fair.olp");
        strcpy(strOlpPoor, strOlp); p = strrchr(strOlpPoor, '.'); if (p) strcpy(p, "_poor.olp");

        // 加载参考影像
        CTifAffine refImg;
        if (!refImg.Load(strRef)) continue;

        char strPlot[512]; strcpy(strPlot, strOlp);
        { char* p = strrchr(strPlot, '.'); if (p) strcpy(p, ".png"); else strcat(strPlot, ".png"); }

        int tieSum = 0, vSum = 0, cSum = 0;
        const int rowsD = domImg.rows, rowsR = refImg.rows;

        olpF.SetSize(0);
		olpExcellent.SetSize(0);
		olpGood.SetSize(0);
		olpFair.SetSize(0);
		olpPoor.SetSize(0);
        // 粗/细分辨率判断
        double gsdD = domImg.GSD(), gsdR_ = refImg.GSD();
        bool domIsCoarse = (gsdD >= gsdR_);
        const CTifAffine& coarseImg = domIsCoarse ? domImg : refImg;
        const CTifAffine& fineImg = domIsCoarse ? refImg : domImg;

        // 分辨率比：粗/细，用于块平均窗口和循环步长换算
        double gsdRatio = coarseImg.GSD() / fineImg.GSD();  // ≥ 1.0

        // 细影像上对应粗影像一个像元的块平均窗口（奇数，上限31）
        int avzFine = int(std::round(gsdRatio));
        if (avzFine % 2 == 0) avzFine++;
        //if (avzFine > 31) avzFine = 31;
        if (avzFine < 3)  avzFine = 0;   // 分辨率相近退化为双线性

        // ── 循环策略：始终以粗影像的格网点为循环基准 ──
        // gs 是粗影像上的步长（像元），循环量 = coarseImg.rows/gs × coarseImg.cols/gs
        // 这样无论分辨率比是多少，循环次数都只由粗影像决定，不会因为细影像很大而爆炸
        const CTifAffine& loopImg = coarseImg;   // 循环基准
        int gsDom = domIsCoarse ? 0 : avzFine;  // dom采样方式
        int gsRef = domIsCoarse ? avzFine : 0;        // ref采样方式

        cprintf("  gsdD=%.4f gsdR=%.4f ratio=%.1f avzFine=%d loopOn=%s\n",
            gsdD, gsdR_, gsdRatio, avzFine, domIsCoarse ? "dom" : "ref");

        // ── 计算两幅影像的空间重叠范围，把循环限定在 overlap 内 ──
        // 避免在哨兵大图的非重叠区域白白遍历
        double x1lo, x1hi, y1lo, y1hi, x2lo, x2hi, y2lo, y2hi;
        domImg.GetBBox(x1lo, x1hi, y1lo, y1hi);
        refImg.GetBBox(x2lo, x2hi, y2lo, y2hi);
        double oxMin = (std::max)(x1lo, x2lo), oxMax = (std::min)(x1hi, x2hi);
        double oyMin = (std::max)(y1lo, y2lo), oyMax = (std::min)(y1hi, y2hi);
        if (oxMin >= oxMax || oyMin >= oyMax) {
            cprintf("  [MchTie] No spatial overlap, skip.\n");
            continue;
        }

        // 把 overlap 四角投影到粗影像像素坐标，取包围盒作为循环范围
        double fc4[4], fr4[4];
        loopImg.Geo2Pix(oxMin, oyMin, fc4[0], fr4[0]);
        loopImg.Geo2Pix(oxMax, oyMin, fc4[1], fr4[1]);
        loopImg.Geo2Pix(oxMin, oyMax, fc4[2], fr4[2]);
        loopImg.Geo2Pix(oxMax, oyMax, fc4[3], fr4[3]);
        int c_start = (std::max)(1, (int)std::ceil(*std::min_element(fc4, fc4 + 4)));
        int c_end = (std::min)(loopImg.cols - step - 1, (int)std::floor(*std::max_element(fc4, fc4 + 4)));
        int r_start = (std::max)(1, (int)std::ceil(*std::min_element(fr4, fr4 + 4)));
        int r_end = (std::min)(loopImg.rows - step - 1, (int)std::floor(*std::max_element(fr4, fr4 + 4)));
        if (c_start >= c_end || r_start >= r_end) {
            cprintf("  [MchTie] Loop range empty, skip.\n"); continue;
        }

        std::vector<MatchRec> allRecs;
        allRecs.reserve(((r_end - r_start) / step + 2) * ((c_end - c_start) / step + 2));

#ifdef _OPENMP
#pragma omp parallel
#endif
        {
            std::vector<MatchRec> local; local.reserve(2048);
            int vLoc = 0, cLoc = 0;
            WORD cv_[16] = {}, rv_[16] = {};

#ifdef _OPENMP
#pragma omp for schedule(dynamic,16) nowait
#endif
            // 在粗影像坐标系上等间距采样
            for (int rr = r_start; rr < r_end; rr += step) {
                for (int cc = c_start; cc < c_end; cc += step) {

                    // 粗影像像素 → 地理坐标
                    double gx, gy; loopImg.Pix2Geo(cc, rr, gx, gy);
                    double fc_d, fr_d, fc_r, fr_r;

                    // LAS 掩膜
                    //if (bHasLas && !lasGrid.IsGround(gx, gy, 1.5)) { vLoc++; continue; }
                    double ptZ = grdZ;
                    if (bHasLas) lasGrid.GetZ(gx, gy, ptZ);

                    // 两幅影像都在粗影像格网点上采样
                    // dom：若是细影像做avzFine块平均；若是粗影像做双线性
                    // ref：同理
                    if (!domImg.SampleGeo(gx, gy, gsDom, cv_, &fc_d, &fr_d)) continue;
                    if (CTifAffine::IsBlack(cv_, domImg.bands)) continue;
                    if (!refImg.SampleGeo(gx, gy, gsRef, rv_, &fc_r, &fr_r)) continue;
                    if (CTifAffine::IsBlack(rv_, refImg.bands)) { vLoc++; continue; }

                    // ZNCC 验证（在粗影像上开窗口）
                    int verifyR = (std::max)(3, RAD_PATCH_RADIUS);
                    bool ok;
                    if (domIsCoarse)
                        ok = VerifyByZNCC(domImg, refImg,
                            int(std::round(fc_d)), int(std::round(fr_d)), verifyR, RAD_MIN_ZNCC);
                    else
                        ok = VerifyByZNCC(refImg, domImg,
                            int(std::round(fc_r)), int(std::round(fr_r)), verifyR, RAD_MIN_ZNCC);
                    if (!ok) { cLoc++; continue; }

                    // 太阳参数
                    double lon, lat, hei, sz, vz, as, sz1 = 0, vz1 = 0, as1 = 0;
                    geoCvt.Cvt_Prj2LBH(gx, gy, ptZ, &lon, &lat, &hei);
                    getSunPos(gx, gy, ptZ, cx, cy, cz,
                        lon * SPGC_R2D, lat * SPGC_R2D,
                        yy, mm, dd, ho, mi, se,
                        &sz, &vz, &as);
                    if (yy1 != 0)
                        getSunPos(gx, gy, ptZ, cx1, cy1, cz1,
                            lon * SPGC_R2D, lat * SPGC_R2D,
                            yy1, mm1, dd1, ho1, mi1, se1, &sz1, &vz1, &as1);

                    MatchRec rec{};
                    rec.cxDom = int(std::round(fc_d)); rec.cyDom = int(std::round(fr_d));
                    rec.cxRef = int(std::round(fc_r)); rec.cyRef = int(std::round(fr_r));
                    for (int b = 0; b < domImg.bands && b < 8; b++) rec.cv[b] = cv_[b];
                    for (int b = 0; b < refImg.bands && b < 8; b++) rec.rv[b] = rv_[b];
                    rec.sz = sz; rec.vz = vz; rec.as = as;
                    rec.sz1 = sz1; rec.vz1 = vz1; rec.as1 = as1;
                    local.emplace_back(rec);
                }
            }
#ifdef _OPENMP
#pragma omp critical
#endif
            { allRecs.insert(allRecs.end(), local.begin(), local.end()); vSum += vLoc; cSum += cLoc; }
        } // end parallel

        // ── 写入 OLP（直接存顶→下像素坐标，不翻转）──
        // idx==-1 表示参考影像为哨兵（绝对约束），rv[]为哨兵DN
        bool isAbsolute = (idx == -1);
        int nEx = 0, nGd = 0, nFr = 0, nPr = 0, nBd = 0;
        double sumCv = 0, sumRv = 0;
        for (auto& rec : allRecs) {
            OBV pt = {
                rec.cxDom, rec.cyDom, rec.cxRef, rec.cyRef,  // cc, cr, rc, rr
                rec.cv[0], rec.cv[1], rec.cv[2], rec.cv[3],  // cv[0-3]
                rec.rv[0], rec.rv[1], rec.rv[2], rec.rv[3],  // rv[0-3]
                (float)rec.sz, (float)rec.vz, (float)rec.as, // csz, cvz, cas
                (float)rec.sz1, (float)rec.vz1, (float)rec.as1 // rsz, rvz, ras
            };

            TiePointQuality quality = EvaluateTiePointQuality(pt, isAbsolute);

            switch (quality) {
            case QUALITY_EXCELLENT: olpExcellent.Append(pt); nEx++; break;
            case QUALITY_GOOD:      olpGood.Append(pt);      nGd++; break;
            case QUALITY_FAIR:      olpFair.Append(pt);      nFr++; break;
            case QUALITY_POOR:      olpPoor.Append(pt);      nPr++; break;
            case QUALITY_BAD:       nBd++; break;
            default: break;
            }
            //if (!IsGoodTiePoint(pt)) {
            //    gSum++;
            //    continue;  // 跳过低质量点
            //}
            olpF.Append(pt);
        }
        tieSum = int(allRecs.size());
        double meanCvStat = (tieSum > 0) ? sumCv / tieSum : 0;
        double meanRvStat = (tieSum > 0) ? sumRv / tieSum : 0;
        // 统计日志：区分绝对约束（哨兵）和相对约束（航空-航空）
        print2Log("%s pair: total=%d Ex=%d(%.0f%%) Gd=%d(%.0f%%) Fr=%d(%.0f%%) Pr=%d(%.0f%%) Bad=%d "
            "meanDN_src=%.0f meanDN_ref=%.0f step=%d\n",
            isAbsolute ? "[ABS/Sentinel]" : "[REL/Aerial]",
            tieSum,
            nEx, tieSum > 0 ? 100.0 * nEx / tieSum : 0.0,
            nGd, tieSum > 0 ? 100.0 * nGd / tieSum : 0.0,
            nFr, tieSum > 0 ? 100.0 * nFr / tieSum : 0.0,
            nPr, tieSum > 0 ? 100.0 * nPr / tieSum : 0.0,
            nBd, meanCvStat, meanRvStat, step);

        // 保存每对的统计文件（.olpstat），供后续分析
        SaveOlpQualityStat(strOlp, isAbsolute, nEx, nGd, nFr, nPr, nBd, meanCvStat, meanRvStat);

        if (nEx + nGd + nFr + nPr > 0) {
            if (bTxt) {
                //char strTxt[512]; strcpy(strTxt, strOlp); strcat(strTxt, ".txt");
                olpF.Save2File(strOlp, FALSE);
				olpExcellent.Save2File(strOlpExcellent, FALSE);
				olpGood.Save2File(strOlpGood, FALSE);
				olpFair.Save2File(strOlpFair, FALSE);
				olpPoor.Save2File(strOlpPoor, FALSE);
            }
            else {
                olpF.Save2File(strOlp);
				olpExcellent.Save2File(strOlpExcellent);
				olpGood.Save2File(strOlpGood);
				olpFair.Save2File(strOlpFair);
				olpPoor.Save2File(strOlpPoor);
            }
            //SaveMatchPlot(domImg, refImg, olpF, strPlot);
        }
    }
    fclose(fTsk);
    return TRUE;
}
