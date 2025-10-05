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

#include <opencv2/opencv.hpp>
//#include <opencv2/core.hpp>
//#include <opencv2/imgproc.hpp>
#include <opencv2/features2d.hpp>
//#include <opencv2/calib3d.hpp>
#pragma comment(lib,"opencv_imgproc490d.lib")
//#define USE_EIGEN_BA 1
#define USE_OPENCV_MATCH

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

#define USE_ITERATIVE_SOLVER 1     // 使用迭代求解器
#define STREAM_PROCESSING 1        // 流式处理模式
#define CHUNK_SIZE 10000          // 数据块大小
#define MAX_SOLVER_ITERATIONS 1000 // 最大迭代次数
#define SOLVER_TOLERANCE 1e-8      // 求解精度

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
    m_utmZn = 49;
	//{{AFX_DATA_INIT(CRadCaliDlg)
	m_strMxCore = _T("8");
	m_strRet = _T("");
	m_strBas = _T("");
	m_gs = 8;
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
#ifdef USE_EIGEN_BA
    pDlg->ProcessWithEigen();
#else
	pDlg->Process();
#endif
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

void CRadCaliDlg::Process()
{
#ifndef ENABLE_OUTLIER_FILTER
#define ENABLE_OUTLIER_FILTER 0          // 1 启用；0 关闭
#endif
#ifndef OUTLIER_SIGMA
#define OUTLIER_SIGMA 3.0                // |l-mean| > 3σ 视为粗差
#endif
#ifndef MIN_OUTLIER_COUNT
#define MIN_OUTLIER_COUNT 30             // 观测少于此数量不做剔除
#endif

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
    
    char strRom[256]; sprintf( strRom,"%s\\ROM_S2Borb",strDir );
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
    if ( m_bAdj ){
        print2Log( "RadBA start...\n" );

        double *pAK1 = new double [sum*2]; 
        double *pAK2 = pAK1+sum;
        memset( pAK1,0,sizeof(double)*sum*2 );
        for( i=0;i<sum;i++ ){
            m_listCtrl.GetItemText(i,0,str,256); 
            sprintf( strT,"%s%s.tsk_skm.txt",strRom,strrchr(str,'\\') );
            FILE *fKM = fopen( strT,"rt");
            fscanf( fKM,"%lf%lf", pAK1+i,pAK2+i );   print2Log("%lf %lf\n",pAK1[i],pAK2[i] );
            fclose(fKM);
        }

        double *aa = new double[ (sum*4)*(sum*4) ]; 
        double *b = new double[ (sum*4) ]; 
        double *a = new double[ (sum*4) ]; 
        double *x = new double[ (sum*4) ];
        
        ProgBegin(sum*4);
        for( c=0;c<4;c++ ){
            UINT st=GetTickCount(); print2Log("proc band %d , Norml ... st= %d\n",c+1,st );

            memset( aa,0,sizeof(double)*(sum*4)*(sum*4) );
            memset( b,0,sizeof(double)*(sum*4) );
            for (i = 0; i < sum; i++, ProgStep(cancel)) {
                if (::WaitForSingleObject(m_hEThEHdl, 1) == WAIT_OBJECT_0) break;

                m_listCtrl.GetItemText(i, 0, str, 256);  print2Log("process[%d @ %d]: %s ", i + 1, sum, strrchr(str, '\\')); UINT ss = GetTickCount();
                sprintf(strT, "%s%s.tsk", strRom, strrchr(str, '\\'));

                FILE* fTsk = fopen(strT, "rt"); if (!fTsk) continue;
                fgets(str, 512, fTsk); sscanf(str, "%s", strSrc); DOS_PATH(strSrc);
                fgets(str, 512, fTsk); sscanf(str, "%d", &idx);
                while (!feof(fTsk)) {
                    if (!fgets(str, 512, fTsk)) break;
                    sscanf(str, "%s", strRef); DOS_PATH(strRef);
                    if (!fgets(str, 512, fTsk)) break;
                    sscanf(str, "%d", &idxr);
                    strcpy(strOlp, strT);  strcpy(strrchr(strOlp, '.'), "_"); strcat(strOlp, strrchr(strRef, '\\') + 1); strcat(strOlp, ".olp");
                    if (olpF.Load4File(strOlp)) {
                        double k1, k2, k1r, k2r, l;
                        int v, oz; OBV* pOs = olpF.GetData(&oz); cprintf("%s tieSum= %d\n", strOlp, oz);
#if ENABLE_OUTLIER_FILTER
                        double sumL = 0.0, sumL2 = 0.0; int cntL = 0;
                        {
                            OBV* pAll = pOs;
                            if (idxr == -1) {
                                for (int vv = 0; vv < oz; ++vv) {
                                    double lRaw = pAll[vv].rv[c];
                                    if (lRaw != 0) { sumL += lRaw; sumL2 += lRaw * lRaw; cntL++; }
                                }
                            }
                            else {
                                for (int vv = 0; vv < oz; ++vv) {
                                    double lRaw = pAll[vv].cv[c] - pAll[vv].rv[c];
                                    sumL += lRaw; sumL2 += lRaw * lRaw; cntL++;
                                }
                            }
                        }
                        double meanL = 0, stdL = 0;
                        bool doFilter = false;
                        if (cntL >= MIN_OUTLIER_COUNT) {
                            meanL = sumL / cntL;
                            double varL = (sumL2 - sumL * sumL / cntL) / ((cntL > 1) ? (cntL - 1) : 1);
                            if (varL > 0) stdL = sqrt(varL);
                            if (stdL > 1e-9) doFilter = true;
                        }
                        int outlierCnt = 0, inlierCnt = 0;
#endif
                        if (idxr == -1) {
                            pOs = olpF.GetData(&oz);
                            for (v = 0; v < oz; v++, pOs++) {
                                k1 = getKval(1, pOs->csz, pOs->cvz, pOs->cas);
                                k2 = getKval(4, pOs->csz, pOs->cvz, pOs->cas);
                                k1 -= pAK1[idx]; k2 -= pAK2[idx];

                                memset(a, 0, sizeof(double) * (sum * 4));
                                l = (pOs->rv[c]); // gb
#if ENABLE_OUTLIER_FILTER
                                if (doFilter) {
                                    double lDev = l - meanL;
                                    if (fabs(lDev) > OUTLIER_SIGMA * stdL) {
                                        outlierCnt++;
                                        continue; // 剔除
                                    }
                                }
                                inlierCnt++;
#endif
                                a[idx * 4 + 0] = -1;
                                a[idx * 4 + 1] = -k1;
                                a[idx * 4 + 2] = -k2;
                                a[idx * 4 + 3] = pOs->cv[c]; // g1
                                Nrml(a, (sum * 4), l, aa, b, 1);
                            }
                        }
                        else {
                            pOs = olpF.GetData(&oz);
                            for (v = 0; v < oz; v++, pOs++) {
                                k1 = getKval(1, pOs->csz, pOs->cvz, pOs->cas);
                                k2 = getKval(4, pOs->csz, pOs->cvz, pOs->cas);
                                k1 -= pAK1[idx]; k2 -= pAK2[idx];

                                k1r = getKval(1, pOs->rsz, pOs->rvz, pOs->ras);
                                k2r = getKval(4, pOs->rsz, pOs->rvz, pOs->ras);
                                k1r -= pAK1[idxr]; k2r -= pAK2[idxr];

                                memset(a, 0, sizeof(double) * (sum * 4));
                                l = pOs->cv[c] - pOs->rv[c]; // g1-g2
#if ENABLE_OUTLIER_FILTER
                                if (doFilter) {
                                    double lDev = l - meanL;
                                    if (fabs(lDev) > OUTLIER_SIGMA * stdL) {
                                        outlierCnt++;
                                        continue;
                                    }
                                }
                                inlierCnt++;
#endif
                                a[idx * 4 + 0] = 1;
                                a[idx * 4 + 1] = k1;
                                a[idx * 4 + 2] = k2;
                                a[idx * 4 + 3] = 0;
                                a[idxr * 4 + 0] = -1;
                                a[idxr * 4 + 1] = -k1r;
                                a[idxr * 4 + 2] = -k2r;
                                a[idxr * 4 + 3] = 0;
                                Nrml(a, (sum * 4), l, aa, b, 0.1);
                            }
                        }
#if ENABLE_OUTLIER_FILTER
                        if (cntL >= MIN_OUTLIER_COUNT) {
                            print2Log("  olp=%s total=%d inlier=%d outlier=%d mean=%.3lf std=%.3lf sigma=%.1f\n",
                                strrchr(strOlp, '\\') ? strrchr(strOlp, '\\') + 1 : strOlp,
                                cntL, inlierCnt, outlierCnt, meanL, stdL, OUTLIER_SIGMA);
                        }
                        else {
                            print2Log("  olp=%s total=%d (skip outlier filter: cnt<%d)\n",
                                strrchr(strOlp, '\\') ? strrchr(strOlp, '\\') + 1 : strOlp,
                                cntL, MIN_OUTLIER_COUNT);
                        }
#endif
                    }
                }
                fclose(fTsk);
                print2Log(", used tm= %.2lf sec \n", (GetTickCount() - ss) * 0.001);
            }
            print2Log("Norml ...over. tm= %.2lf sec \nSolve ... st= %d \n",(GetTickCount()-st)*0.001,GetTickCount() ); 

            memset( x,0,sizeof(double)*(sum*4) );  st = GetTickCount();
            Solve( aa,b,x,(sum*4),(sum*4) );

            print2Log( "Solve .. over. tm = %.2lf \n",(GetTickCount()-st)*0.001 );

                    sprintf(str, "%s//pre_bnd%d.txt", strRom, c + 1);
            FILE *fKnl = fopen( str,"wt" );
            for( i=0;i<sum;i++ ){
                m_listCtrl.GetItemText(i,0,str,256);
                fprintf( fKnl,"%9.6lf \t %15.6lf \t %15.6lf \t %15.6lf \t %s\n",x[i*4+3],x[i*4+0],x[i*4+1],x[i*4+2],strrchr(str,'\\') );
                    }
                    fclose(fKnl);
                }
        delete []pAK1;
        delete []aa; 
        delete []a;
        delete []b;
        delete []x;
        
        ProgEnd();   
        print2Log( "RadBA over.\n" );
    }

   	if (m_hW4EndHdl) ::SetEvent( m_hW4EndHdl );
    if (m_hEThEHdl ) ::CloseHandle(m_hEThEHdl); m_hEThEHdl = NULL;
	::CloseHandle( m_hThread ); m_hThread = NULL;
    
    PostMessage( WM_OUTPUT_MSG,THREADEND,0 );
}

void CRadCaliDlg::ProcessWithEigen()
{
    gs_hWnd = m_hWnd;
    m_hEThEHdl = ::CreateEvent(NULL, TRUE, FALSE, itostr(LONG(this)));

    CTime stTm; SYSTEM_INFO sysInfo; GetSystemInfo(&sysInfo);
    if (sysInfo.dwNumberOfProcessors > MAX_CPU) sysInfo.dwNumberOfProcessors = MAX_CPU;
    BOOL bRun = FALSE; int maxTm = 120, cpuSum = atoi(m_strMxCore);
    if (cpuSum < 1) cpuSum = 1;  if (cpuSum > sysInfo.dwNumberOfProcessors - 1) cpuSum = sysInfo.dwNumberOfProcessors - 1;
    char strCmd[1024], strExe[256]; ::GetModuleFileName(NULL, strExe, sizeof(strExe));

    m_strMxCore.Format("%d", cpuSum);
    AfxGetApp()->WriteProfileString("CRadCaliDlg", "CPUs", m_strMxCore);

    char* pS, strDir[256], strT[256], str[512];
    strcpy(strDir, m_strRet); pS = strrchr(strDir, '\\'); if (pS) *pS = 0;

    char strRom[256]; sprintf(strRom, "%s\\ROM_S2Bexp", strDir);
    CreateDir(strRom);

    char strLog[256]; strcpy(strLog, m_strRet);
    sprintf(str, "%s-s2b-eigen.log", strLog);
    ::DeleteFile(str);  openLog(str);

    int i, j, c, b, sum = m_listCtrl.GetItemCount();

    struct RI {
        int idx;
        float area;
    }; RI iRi, * pRi = new RI[sum + 8];
    struct RGN {
        double x[8];
        double y[8];
        int sz;
    }; RGN iRg, * pRg = new RGN[sum + 8];
    memset(pRg, 0, sizeof(RGN) * sum);
    IMGPAR imgPar; CTMGeom cc;
    double xs, ys, zs, phi, omg, kap, grdZ;
    sprintf(strT, "%s\\imgPar.dpi", strDir); DOS_PATH(strT);
    CTMVziFile vziFile; vziFile.Load4File(strT);
    for (i = 0; i < sum; i++) {
        m_listCtrl.GetItemText(i, 1, str, 256);
        imgPar = vziFile.m_imgPar;
        sscanf(str, "%lf%lf%lf%lf%lf%lf%lf", &xs, &ys, &zs, &phi, &omg, &kap, &grdZ);
        imgPar.aopX = xs; imgPar.aopY = ys; imgPar.aopZ = zs;
        imgPar.aopP = phi; imgPar.aopO = omg; imgPar.aopK = kap;
        cc.Init(&imgPar); pRg[i].sz = 4;
        cc.GetGrdPrjRgn(imgPar.iopX * 2, imgPar.iopY * 2, grdZ, pRg[i].x, pRg[i].y);
    }

    if (m_bTie) print2Log("MchTie start...\n");

    ProgBegin(sum); int cancel;
    for (i = 0; i < sum; i++, ProgStep(cancel)) {
        if (::WaitForSingleObject(m_hEThEHdl, 1) == WAIT_OBJECT_0) break;

        m_listCtrl.GetItemText(i, 0, str, 256);  print2Log("process: %s\n", strrchr(str, '\\'));
        sprintf(strT, "%s%s.tsk", strRom, strrchr(str, '\\'));
        FILE* fTsk = fopen(strT, "wt");
        fprintf(fTsk, "%s\n%d %s %d %d %d\n", str, i, m_listCtrl.GetItemText(i, 1), m_gs, m_ws, m_bTxt);
        fprintf(fTsk, "%s\n%s\n", m_strBas, "-1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 ");

        iRi.area = (float)(GetRgnArea(pRg[i].x, pRg[i].y, pRg[i].sz));
        memset(pRi, 0, sizeof(RI) * sum);
        for (j = 0; j < sum; j++) {
            if (j == i) continue;
            iRg.sz = 0;
            RgnClip(pRg[i].x, pRg[i].y, pRg[i].sz,
                pRg[j].x, pRg[j].y, pRg[j].sz,
                iRg.x, iRg.y, &iRg.sz);
            if (iRg.sz > 2) {
                pRi[j].idx = j;
                pRi[j].area = (float)(GetRgnArea(iRg.x, iRg.y, iRg.sz));
            }
        }
        qsort(pRi, sum, sizeof(RI), &comRI);
        for (j = 0; j < sum; j++) {
            if (pRi[j].area / iRi.area < 0.01) break;
            fprintf(fTsk, "%s\n", m_listCtrl.GetItemText(pRi[j].idx, 0));
            fprintf(fTsk, "%d %s\n", pRi[j].idx, m_listCtrl.GetItemText(pRi[j].idx, 1));
        }
        fclose(fTsk);

        if (m_bTie) {
#ifdef _DEBUG
            sprintf(strCmd, "TIE@%s", strT);
            MchTie(strCmd);
#else
            sprintf(strCmd, "%s TIE@%s", strExe, strT);
            print2Log("%s\n", strCmd);
#endif

            bRun = FALSE;
            while (!bRun) {
                for (int c = 0; c < cpuSum; c++) {
                    if (gs_hThreadId[c] == NULL) {
                        strcpy(gs_strCmd[c], strCmd);
                        HANDLE hThread = ::CreateThread(NULL, 0, CupThread_CRadCaliDlg, (void*)gs_strCmd[c], 0, gs_hThreadId + c);
                        ::CloseHandle(hThread); bRun = TRUE; break;
                    }
                    if (::WaitForSingleObject(m_hEThEHdl, 16) == WAIT_OBJECT_0) break;
                }
            }
        }
    }
    delete[]pRi;
    delete[]pRg;

    stTm = CTime::GetCurrentTime();
    while (1) {
        for (c = 0; c < MAX_CPU; c++) { if (gs_hThreadId[c] != NULL) break; }
        if (c == MAX_CPU || ::WaitForSingleObject(m_hEThEHdl, 128) == WAIT_OBJECT_0) break;
        CTimeSpan ts = CTime::GetCurrentTime() - stTm;
        if (ts.GetTotalMinutes() > maxTm) {
            for (int i = 0; i < MAX_CPU; i++) {
                if (gs_hProcId[i]) {
                    HANDLE hProc = ::OpenProcess(PROCESS_TERMINATE, FALSE, gs_hProcId[i]);
                    if (hProc) ::TerminateProcess(hProc, 0x22);
                    gs_hProcId[i] = 0; Sleep(8);
                }
            }
            break;
        }
    }
    if (m_bTie) print2Log("MchTie over.\n");
    ProgEnd();

    COlpFile olpF; char strSrc[256], strRef[256], strOlp[512]; int idx, idxr;
    if (m_bAdj) {
        print2Log("RadBA with Eigen start...\n");

        double* pAK1 = new double[sum * 2];
        double* pAK2 = pAK1 + sum;
        memset(pAK1, 0, sizeof(double)* sum * 2);
        for (i = 0; i < sum; i++) {
            m_listCtrl.GetItemText(i, 0, str, 256);
            sprintf(strT, "%s%s.tsk_skm.txt", strRom, strrchr(str, '\\'));
            FILE* fKM = fopen(strT, "rt");
            fscanf(fKM, "%lf%lf", pAK1 + i, pAK2 + i);   print2Log("%lf %lf\n", pAK1[i], pAK2[i]);
            fclose(fKM);
        }

        ProgBegin(sum * 4);
        for (c = 0; c < 4; c++) {
            UINT st = GetTickCount();
            print2Log("proc band %d with Eigen, st= %d\n", c + 1, st);

            Eigen::SparseMatrix<double> AtA(sum * 4, sum * 4);
            Eigen::VectorXd Atb = Eigen::VectorXd::Zero(sum * 4);

            for (i = 0; i < sum; i++, ProgStep(cancel)) {
                if (::WaitForSingleObject(m_hEThEHdl, 1) == WAIT_OBJECT_0) break;

                m_listCtrl.GetItemText(i, 0, str, 256); print2Log("process[%d @ %d]: %s ", i + 1, sum, strrchr(str, '\\')); UINT ss = GetTickCount();
                sprintf(strT, "%s%s.tsk", strRom, strrchr(str, '\\'));

                FILE* fTsk = fopen(strT, "rt");
                if (!fTsk) continue;

                fgets(str, 512, fTsk); sscanf(str, "%s", strSrc); DOS_PATH(strSrc);
                fgets(str, 512, fTsk); sscanf(str, "%d", &idx);

                std::vector<Eigen::Triplet<double>> triplets;

                while (!feof(fTsk)) {
                    if (!fgets(str, 512, fTsk)) break;
                    sscanf(str, "%s", strRef); DOS_PATH(strRef);
                    if (!fgets(str, 512, fTsk)) break;
                    sscanf(str, "%d", &idxr);
                    strcpy(strOlp, strT);  strcpy(strrchr(strOlp, '.'), "_");
                    strcat(strOlp, strrchr(strRef, '\\') + 1); strcat(strOlp, ".olp");

                    if (olpF.Load4File(strOlp)) {
                        double k1, k2, k1r, k2r, l;
                        int v, oz; OBV* pOs = olpF.GetData(&oz); cprintf("%s tieSum= %d\n", strOlp, oz);

                        if (idxr == -1) {
                            for(v=0;v<oz;v++,pOs++){
                                k1 = getKval(1, pOs->csz, pOs->cvz, pOs->cas);
                                k2 = getKval(4, pOs->csz, pOs->cvz, pOs->cas);
                                k1 -= pAK1[idx]; k2 -= pAK2[idx];
								l = pOs->rv[c];// gb
                                std::vector<std::pair<int, double>> a_nonzeros;
                                a_nonzeros.push_back({ idx * 4 + 0, -1 });
                                a_nonzeros.push_back({ idx * 4 + 1, -k1 });
                                a_nonzeros.push_back({ idx * 4 + 2, -k2 });
                                a_nonzeros.push_back({ idx * 4 + 3, pOs->cv[c] });

                                for (const auto& ai : a_nonzeros) {
                                    for (const auto& aj : a_nonzeros) {
                                        triplets.push_back(Eigen::Triplet<double>(ai.first, aj.first, ai.second * aj.second));
                                    }
                                    Atb[ai.first] += ai.second * l;
                                }
                            }
                        }
                        else {
                            for (v = 0; v < oz; v++, pOs++) {
                                k1 = getKval(1, pOs->csz, pOs->cvz, pOs->cas);
                                k2 = getKval(4, pOs->csz, pOs->cvz, pOs->cas);
                                k1 -= pAK1[idx]; k2 -= pAK2[idx];
                                k1r = getKval(1, pOs->rsz, pOs->rvz, pOs->ras);
                                k2r = getKval(4, pOs->rsz, pOs->rvz, pOs->ras);
                                k1r -= pAK1[idxr]; k2r -= pAK2[idxr];
                                l = pOs->cv[c] - pOs->rv[c];
                                std::vector<std::pair<int, double>> a_nonzeros;
                                a_nonzeros.push_back({ idx * 4 + 0, 1 });
                                a_nonzeros.push_back({ idx * 4 + 1, k1 });
                                a_nonzeros.push_back({ idx * 4 + 2, k2 });
                                a_nonzeros.push_back({ idxr * 4 + 0, -1 });
                                a_nonzeros.push_back({ idxr * 4 + 1, -k1r });
                                a_nonzeros.push_back({ idxr * 4 + 2, -k2r });

                                for (const auto& ai : a_nonzeros) {
                                    for (const auto& aj : a_nonzeros) {
                                        triplets.push_back(Eigen::Triplet<double>(ai.first, aj.first, ai.second * aj.second));
                                    }
                                    Atb[ai.first] += ai.second * l;
                                }
                            }
                        }

                    }
                }
                fclose(fTsk);
                Eigen::SparseMatrix<double> temp_AtA(sum * 4, sum * 4);
                temp_AtA.setFromTriplets(triplets.begin(), triplets.end());

                AtA += temp_AtA;
                print2Log(", used tm= %.2lf sec\n", (GetTickCount() - ss) * 0.001);
            }
            print2Log("Norml ...over. tm= %.2lf sec \nSolve ... st= %d \n", (GetTickCount() - st) * 0.001, GetTickCount());

            st = GetTickCount();
            print2Log("Solving with iterative solver...\n");

            Eigen::VectorXd x = Eigen::VectorXd::Zero(sum * 4);

            bool solved = false;

            // 共轭梯度法
            if (!solved) {
                Eigen::ConjugateGradient<Eigen::SparseMatrix<double>, Eigen::Lower | Eigen::Upper> cg;
                cg.setMaxIterations(MAX_SOLVER_ITERATIONS);
                cg.setTolerance(SOLVER_TOLERANCE);
                cg.compute(AtA);

                if (cg.info() == Eigen::Success) {
                    x = cg.solve(Atb);
                    if (cg.info() == Eigen::Success) {
                        print2Log("ConjugateGradient solve success. Iterations: %ld, Error: %.2e, tm: %.2lf sec\n",
                            cg.iterations(), cg.error(), (GetTickCount() - st) * 0.001);
                        solved = true;
                    }
                }
            }

            //双共轭梯度稳定法（适合非对称系统）
            if (!solved) {
                Eigen::BiCGSTAB<Eigen::SparseMatrix<double>> bicgstab;
                bicgstab.setMaxIterations(MAX_SOLVER_ITERATIONS);
                bicgstab.setTolerance(SOLVER_TOLERANCE);
                bicgstab.compute(AtA);

                if (bicgstab.info() == Eigen::Success) {
                    x = bicgstab.solve(Atb);
                    if (bicgstab.info() == Eigen::Success) {
                        print2Log("BiCGSTAB solve success. Iterations: %ld, Error: %.2e, tm: %.2lf sec\n",
                            bicgstab.iterations(), bicgstab.error(), (GetTickCount() - st) * 0.001);
                        solved = true;
                    }
                }
            }

            //稀疏LU分解（内存消耗较大但最稳定）
            if (!solved) {
                print2Log("Iterative solvers failed, using Sparse LU...\n");
                Eigen::SparseLU<Eigen::SparseMatrix<double>> sparseLU;
                sparseLU.compute(AtA);

                if (sparseLU.info() == Eigen::Success) {
                    x = sparseLU.solve(Atb);
                    if (sparseLU.info() == Eigen::Success) {
                        print2Log("SparseLU solve success. tm: %.2lf sec\n", (GetTickCount() - st) * 0.001);
                        solved = true;
                    }
                }
            }

            if (!solved) {
                print2Log("All solvers failed for band %d!\n", c + 1);
                continue;
            }

            sprintf(str, "%s//pre_bnd%d_eigen.txt", strRom, c + 1);
            FILE* fKnl = fopen(str, "wt");
            if (fKnl) {
                for (i = 0; i < sum; i++) {
                    m_listCtrl.GetItemText(i, 0, str, 256);
                    fprintf(fKnl, "%9.6lf \t %15.6lf \t %15.6lf \t %15.6lf \t %s\n",
                        x[i * 4 + 3], x[i * 4 + 0], x[i * 4 + 1], x[i * 4 + 2],
                        strrchr(str, '\\'));
                }
                fclose(fKnl);
            }
        }
        ProgEnd();
        print2Log("RadBA with Eigen completed.\n");
    }

    if (m_hW4EndHdl) ::SetEvent(m_hW4EndHdl);
    if (m_hEThEHdl) ::CloseHandle(m_hEThEHdl); m_hEThEHdl = NULL;
    ::CloseHandle(m_hThread); m_hThread = NULL;

    PostMessage(WM_OUTPUT_MSG, THREADEND, 0);
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




#include "WuErsImage.hpp"
#include "WuBmpFile.hpp"

inline WORD* GetBiPxlRS(float fx, float fy, int cols, int rows, int bnds,WORD *pBuf, WORD bkGrd = 0){    
    WORD *buf,buf00[16]={bkGrd,bkGrd,bkGrd,bkGrd,bkGrd,bkGrd,bkGrd,bkGrd};
    static WORD buf0[16]; memcpy(buf0,buf00,sizeof(buf0)); buf= buf0; 
    int b,r1, r2, r3, r4, col = int(fx), row = int(fy);
    if (col < 0 || row < 0 || col >= cols-1 || row >= rows-1)  return buf;
    float dx = fx - col, dy = fy - row;
    WORD *p, *p0 = pBuf + (INT64(row)*cols + col)*bnds;
    if (dx < 0.05f && dy < 0.05f) return p0;
    p= p0 +0; r1 =*p ; r2 = *(p+bnds); p += cols*bnds; r3 =*p; r4 = *(p+bnds);
    buf[0] = WORD( (1-dx)*(1-dy)*r1+dx*(1-dy)*r2+(1-dx)*dy*r3+dx*dy*r4 );
    for( b=1;b<bnds;b++ ){
        p= p0+b; r1 = *p ; r2 = *(p+bnds); p += cols*bnds; r3 =*p; r4 = *(p+bnds);
        buf[b] = WORD( (1-dx)*(1-dy)*r1+dx*(1-dy)*r2+(1-dx)*dy*r3+dx*dy*r4 );        
    }
    return buf;
}


inline WORD* GetAvPxlRS(float fx, float fy, int cols, int rows, int bnds,WORD *pBuf, int avz,WORD bkGrd = 0){    
    WORD *buf,buf00[16]={bkGrd,bkGrd,bkGrd,bkGrd,bkGrd,bkGrd,bkGrd,bkGrd};
    static WORD buf0[16]; memcpy(buf0,buf00,sizeof(buf0)); buf= buf0;
    int col = int(fx), row = int(fy),az=avz/2; 
    if (col < az || row < az || col >= cols-1-az || row >= rows-1-az )  return buf;
    int r,c,b,bs,s=0,sv[16]={0,0,0,0,0,0,0,}; WORD *p, *p0;
    for( r=0;r<avz;r++ ){
        p0 = pBuf+(INT64(row-az+r)*cols + col-az)*bnds;
        for( p=p0,c=0;c<avz;c++ ){
            for( bs=0,b=0;b<bnds;b++,p++ ){
                sv[b]+=p[b]; if (p[b]) bs++;
            }
            if (bs) s++;
        }
    }
    if (s>az){ for( b=0;b<bnds;b++){ buf[b] = sv[b]/s; } }
    return buf;
}

static bool GetPxl(double gx,double gy,CWuErsImage *pImg,short *pv,int gs,double *fc0=NULL,double *fr0=NULL){

    int cols = pImg->GetCols();
    int rows = pImg->GetRows();
    int bnds = pImg->GetBands();
    int pxSz = pImg->PxByte( pImg->GetPxType() )*bnds;    
    double fc = (gx - pImg->m_tlX)/pImg->m_dx; 
    double fr = (rows-1) - (pImg->m_tlY - gy)/pImg->m_dy; 
    if(fc0)*fc0 = fc; if(fr0)*fr0 = fr;    

    WORD *pC = NULL;
    if ( gs ) pC = GetAvPxlRS( fc,fr,cols,rows,bnds,(WORD*)pImg->m_pImgDat,gs,0 );
    else pC = GetBiPxlRS( fc,fr,cols,rows,bnds,(WORD*)pImg->m_pImgDat,0 );
    pv[0]=pC[0]; pv[1]=pC[1]; pv[2]=pC[2]; pv[3]=pC[3];
    return true;
}

// 辐射相关性计算函数
static inline double CalculateRadiationCorrelation(short cv[4], short rv[4]) {
    double sum1 = 0, sum2 = 0, sum11 = 0, sum22 = 0, sum12 = 0;
    int valid_bands = 0;

    for (int b = 0; b < 4; b++) {
        if (cv[b] > 0 && rv[b] > 0) { // 只处理有效波段
            sum1 += cv[b];
            sum2 += rv[b];
            sum11 += cv[b] * cv[b];
            sum22 += rv[b] * rv[b];
            sum12 += cv[b] * rv[b];
            valid_bands++;
        }
    }

    if (valid_bands < 2) return 0; // 至少需要两个有效波段

    double numerator = valid_bands * sum12 - sum1 * sum2;
    double denominator = sqrt((valid_bands * sum11 - sum1 * sum1) *
        (valid_bands * sum22 - sum2 * sum2));

    return fabs(denominator) > 1e-6 ? numerator / denominator : 0;
}

// 计算主/参考的像元尺度比（>1 表示参考更粗，需要下采主影像）
static inline double ComputeScaleHint(const CWuErsImage& domImg, const CWuErsImage& refImg)
{
    double sx = fabs(refImg.m_dx / domImg.m_dx);
    double sy = fabs(refImg.m_dy / domImg.m_dy);
    if (!std::isfinite(sx) || sx <= 0) sx = 1.0;
    if (!std::isfinite(sy) || sy <= 0) sy = 1.0;
    return 0.5 * (sx + sy);
}
// ========== 计算重叠掩膜（像素坐标） ==========
static inline bool BuildOverlapMasks(CWuErsImage& dom,
    CWuErsImage& ref,
    cv::Mat& maskDom,
    cv::Mat& maskRef,
    int pad = 16)
{
    // dom世界范围
    const double d_left = dom.m_tlX;
    const double d_right = dom.m_tlX + (dom.GetCols() - 1) * dom.m_dx;
    const double d_top = dom.m_tlY;
    const double d_bottom = dom.m_tlY - (dom.GetRows() - 1) * dom.m_dy;
    // ref世界范围
    const double r_left = ref.m_tlX;
    const double r_right = ref.m_tlX + (ref.GetCols() - 1) * ref.m_dx;
    const double r_top = ref.m_tlY;
    const double r_bottom = ref.m_tlY - (ref.GetRows() - 1) * ref.m_dy;

    // 交集（世界坐标）
    const double wxmin = std::max(d_left, r_left);
    const double wxmax = std::min(d_right, r_right);
    const double wymax = std::min(d_top, r_top);
    const double wymin = std::max(d_bottom, r_bottom);
    if (!(wxmin < wxmax && wymin < wymax)) {
        maskDom = cv::Mat(); maskRef = cv::Mat();
        return false;
    }

    // 世界->像素（列：x方向；行：自上而下）
    auto clamp = [](int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); };

    // dom像素矩形
    int d_cL = (int)std::floor((wxmin - dom.m_tlX) / dom.m_dx) - pad;
    int d_cR = (int)std::ceil((wxmax - dom.m_tlX) / dom.m_dx) + pad;
    int d_rT = (int)std::floor((dom.m_tlY - wymax) / dom.m_dy) - pad; // 注意顶->小行号
    int d_rB = (int)std::ceil((dom.m_tlY - wymin) / dom.m_dy) + pad;

    d_cL = clamp(d_cL, 0, dom.GetCols() - 1);
    d_cR = clamp(d_cR, 0, dom.GetCols() - 1);
    d_rT = clamp(d_rT, 0, dom.GetRows() - 1);
    d_rB = clamp(d_rB, 0, dom.GetRows() - 1);
    if (d_cL >= d_cR || d_rT >= d_rB) {
        maskDom = cv::Mat(); maskRef = cv::Mat();
        return false;
    }

    // ref像素矩形
    int r_cL = (int)std::floor((wxmin - ref.m_tlX) / ref.m_dx) - pad;
    int r_cR = (int)std::ceil((wxmax - ref.m_tlX) / ref.m_dx) + pad;
    int r_rT = (int)std::floor((ref.m_tlY - wymax) / ref.m_dy) - pad;
    int r_rB = (int)std::ceil((ref.m_tlY - wymin) / ref.m_dy) + pad;

    r_cL = clamp(r_cL, 0, ref.GetCols() - 1);
    r_cR = clamp(r_cR, 0, ref.GetCols() - 1);
    r_rT = clamp(r_rT, 0, ref.GetRows() - 1);
    r_rB = clamp(r_rB, 0, ref.GetRows() - 1);
    if (r_cL >= r_cR || r_rT >= r_rB) {
        maskDom = cv::Mat(); maskRef = cv::Mat();
        return false;
    }

    maskDom = cv::Mat::zeros(dom.GetRows(), dom.GetCols(), CV_8U);
    maskRef = cv::Mat::zeros(ref.GetRows(), ref.GetCols(), CV_8U);
    cv::rectangle(maskDom, cv::Rect(d_cL, d_rT, d_cR - d_cL + 1, d_rB - d_rT + 1), cv::Scalar(255), cv::FILLED);
    cv::rectangle(maskRef, cv::Rect(r_cL, r_rT, r_cR - r_cL + 1, r_rB - r_rT + 1), cv::Scalar(255), cv::FILLED);
    return true;
}

#ifdef USE_OPENCV_MATCH
static void WuErsToGray8(CWuErsImage& img, cv::Mat& outGray)
{
    int cols = img.GetCols();
    int rows = img.GetRows();
    int bands = img.GetBands();
    const WORD* pSrc = (const WORD*)img.m_pImgDat;
    outGray.create(rows, cols, CV_8U);
    for (int r = 0; r < rows; ++r) {
        const WORD* rowPtr = pSrc + (INT64)r * cols * bands;
        uint8_t* gPtr = outGray.ptr<uint8_t>(r);
        for (int c = 0; c < cols; ++c) {
            const WORD* px = rowPtr + c * bands;
            uint32_t s = 0; int valid = 0;
            for (int b = 0; b < bands && b < 3; b++) {
                if (px[b] > 0) { s += px[b]; valid++; }
            }
            if (valid == 0) { gPtr[c] = 0; }
            else {
                WORD v = (WORD)(s / valid);
                gPtr[c] = (uint8_t)(v >> 8); // 16→8
            }
        }
    }
}
// 从 .olp 绘制匹配连线图（.png）
static void SaveMatchesPlotFromOlp(CWuErsImage& domImg,
    CWuErsImage& refImg,
    COlpFile& olp,
    const char* outPath)
{
    int oz = 0; const OBV* p = olp.GetData(&oz);
    if (!p || oz <= 0) return;

    cv::Mat gL0, gR0;
    WuErsToGray8(domImg, gL0);
    WuErsToGray8(refImg, gR0);

    // 基于像元大小做显示尺度归一：统一到较粗分辨率
    const double gsdL = std::abs(domImg.m_dx);
    const double gsdR = std::abs(refImg.m_dx);
    const double gsdCoarse = std::max(gsdL, gsdR);
    double sL = (gsdL > 0) ? (gsdL / gsdCoarse) : 1.0; // <=1
    double sR = (gsdR > 0) ? (gsdR / gsdCoarse) : 1.0; // <=1
    // 避免宽或高为0
    auto safeSize = [](int v, double s) { return std::max(1, (int)std::round(v * s)); };

    cv::Mat gL, gR;
    cv::resize(gL0, gL, cv::Size(safeSize(gL0.cols, sL), safeSize(gL0.rows, sL)), 0, 0, cv::INTER_AREA);
    cv::resize(gR0, gR, cv::Size(safeSize(gR0.cols, sR), safeSize(gR0.rows, sR)), 0, 0, cv::INTER_AREA);

    const int WL = gL.cols, WR = gR.cols;
    const int H = std::max(gL.rows, gR.rows);

    cv::Mat canvas(H, WL + WR, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::Mat roiL = canvas(cv::Rect(0, 0, WL, gL.rows));
    cv::Mat roiR = canvas(cv::Rect(WL, 0, WR, gR.rows));
    cv::cvtColor(gL, roiL, cv::COLOR_GRAY2BGR);
    cv::cvtColor(gR, roiR, cv::COLOR_GRAY2BGR);

    cv::RNG rng(12345);
    for (int i = 0; i < oz; ++i, ++p) {
        // .olp 存的是底->上的行号，这里转回顶->下，再乘以显示缩放
        double rL_top = (gL0.rows - 1 - p->cr) * sL;
        double rR_top = (gR0.rows - 1 - p->rr) * sR;
        double cL_s = p->cc * sL;
        double cR_s = p->rc * sR;

        if (cL_s < 0 || rL_top < 0 || cL_s >= WL || rL_top >= gL.rows) continue;
        if (cR_s < 0 || rR_top < 0 || cR_s >= WR || rR_top >= gR.rows) continue;

        cv::Point2f pL((float)cL_s, (float)rL_top);
        cv::Point2f pR((float)(cR_s + WL), (float)rR_top);

        cv::Scalar col(rng.uniform(0, 255), rng.uniform(0, 255), rng.uniform(0, 255));
        cv::circle(canvas, pL, 2, col, -1, cv::LINE_AA);
        cv::circle(canvas, pR, 2, col, -1, cv::LINE_AA);
        cv::line(canvas, pL, pR, col, 1, cv::LINE_AA);
    }
    cv::imwrite(outPath, canvas);
}

static void SaveMatchesPlotVec(CWuErsImage& domImg,
    CWuErsImage& refImg,
    const std::vector<cv::Point2f>& ptsDom,
    const std::vector<cv::Point2f>& ptsRef,
    const char* outPath)
{
    if (ptsDom.empty() || ptsDom.size() != ptsRef.size()) return;

    cv::Mat gL0, gR0;
    WuErsToGray8(domImg, gL0);
    WuErsToGray8(refImg, gR0);

    const double gsdL = std::abs(domImg.m_dx);
    const double gsdR = std::abs(refImg.m_dx);
    const double gsdCoarse = std::max(gsdL, gsdR);
    double sL = (gsdL > 0) ? (gsdL / gsdCoarse) : 1.0;
    double sR = (gsdR > 0) ? (gsdR / gsdCoarse) : 1.0;
    auto safeSize = [](int v, double s) { return std::max(1, (int)std::round(v * s)); };

    cv::Mat gL, gR;
    cv::resize(gL0, gL, cv::Size(safeSize(gL0.cols, sL), safeSize(gL0.rows, sL)), 0, 0, cv::INTER_AREA);
    cv::resize(gR0, gR, cv::Size(safeSize(gR0.cols, sR), safeSize(gR0.rows, sR)), 0, 0, cv::INTER_AREA);

    const int WL = gL.cols, WR = gR.cols;
    const int H = std::max(gL.rows, gR.rows);

    cv::Mat canvas(H, WL + WR, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::Mat roiL = canvas(cv::Rect(0, 0, WL, gL.rows));
    cv::Mat roiR = canvas(cv::Rect(WL, 0, WR, gR.rows));
    cv::cvtColor(gL, roiL, cv::COLOR_GRAY2BGR);
    cv::cvtColor(gR, roiR, cv::COLOR_GRAY2BGR);

    cv::RNG rng(12345);
    for (size_t i = 0; i < ptsDom.size(); ++i) {
        cv::Point2f pL(ptsDom[i].x * (float)sL, ptsDom[i].y * (float)sL);
        cv::Point2f pR(ptsRef[i].x * (float)sR + WL, ptsRef[i].y * (float)sR);

        if (pL.x < 0 || pL.y < 0 || pL.x >= WL || pL.y >= gL.rows) continue;
        if (pR.x < WL || pR.y < 0 || pR.x >= WL + WR || pR.y >= gR.rows) continue;

        cv::Scalar col(rng.uniform(0, 255), rng.uniform(0, 255), rng.uniform(0, 255));
        cv::circle(canvas, pL, 2, col, -1, cv::LINE_AA);
        cv::circle(canvas, pR, 2, col, -1, cv::LINE_AA);
        cv::line(canvas, pL, pR, col, 1, cv::LINE_AA);
    }
    cv::imwrite(outPath, canvas);
}

static inline void ApplyCLAHE(cv::Mat& img) {
    // 遥感图像动态范围大，CLAHE比全局均衡更稳
    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(3.0, cv::Size(32, 32));
    clahe->apply(img, img);
}

struct ORBMatchResult {
    std::vector<cv::Point2f> ptsDom; // 主影像
    std::vector<cv::Point2f> ptsRef; // 参考影像
};

static ORBMatchResult RunORBMatch(CWuErsImage& domImg,
    CWuErsImage& refImg,
    int maxFeatures = 40000,
    float scaleFactor = 1.2f,
    int nLevels = 8,
    float ratio = 0.75f,
    double ransacThresh = 2.0,
    double ransacConf = 0.999,
    const cv::Mat* maskDom = nullptr,
    const cv::Mat* maskRef = nullptr)
{
    ORBMatchResult res;

    cv::Mat gDom, gRef;
    WuErsToGray8(domImg, gDom);
    WuErsToGray8(refImg, gRef);

    ApplyCLAHE(gDom);
    ApplyCLAHE(gRef);

    cv::Ptr<cv::ORB> orb = cv::ORB::create(maxFeatures, scaleFactor, nLevels);
    std::vector<cv::KeyPoint> kptsDom, kptsRef;
    cv::Mat descDom, descRef;
    if (maskDom && !maskDom->empty())
        orb->detectAndCompute(gDom, *maskDom, kptsDom, descDom);
    else
        orb->detectAndCompute(gDom, cv::noArray(), kptsDom, descDom);

    if (maskRef && !maskRef->empty())
        orb->detectAndCompute(gRef, *maskRef, kptsRef, descRef);
    else
        orb->detectAndCompute(gRef, cv::noArray(), kptsRef, descRef);

    if (descDom.empty() || descRef.empty()) return res;

    cv::BFMatcher matcher(cv::NORM_HAMMING, false);
    std::vector<std::vector<cv::DMatch>> knn;
    matcher.knnMatch(descDom, descRef, knn, 2);

    std::vector<cv::Point2f> q1, q2;
    q1.reserve(knn.size());
    q2.reserve(knn.size());

    for (auto& k : knn) {
        if (k.size() < 2) continue;
        if (k[0].distance < ratio * k[1].distance) {
            q1.push_back(kptsDom[k[0].queryIdx].pt);
            q2.push_back(kptsRef[k[0].trainIdx].pt);
        }
    }
    if (q1.size() < 8) return res;

    std::vector<unsigned char> inlierMask;
    cv::Mat F = cv::findFundamentalMat(q1, q2, cv::FM_RANSAC, ransacThresh, ransacConf, inlierMask);
    if (F.empty()) return res;

    for (size_t i = 0; i < q1.size(); ++i) {
        if (inlierMask[i]) {
            res.ptsDom.push_back(q1[i]);
            res.ptsRef.push_back(q2[i]);
        }
    }
    return res;
}
#endif

BOOL MchTie(LPCSTR lpstrPar)
{
    char strSrc[256], strRef[256], strTsk[256], str[512], strOlp[512];
    double fc, fr, fc1, fr1, sz, vz, as, sz1, vz1, as1;
    double cx, cy, cz, phi, img, kap, grdZ, cx1, cy1, cz1, phi1, img1, kap1, grdZ1;
    int idx, yy, mm, dd, ho, mi, se, yy1, mm1, dd1, ho1, mi1, se1, utmZn = 0, gs = 21, ws = 5, bTxt = 0;

    double minCorr = 0.7;    // 辐射相关性最小阈值

    const char* pS = strrchr(lpstrPar, '@'); if (pS) { pS++; while (*pS == ' ') pS++; }
    else pS = lpstrPar; strcpy(strTsk, pS);
    FILE* fTsk = fopen(strTsk, "rt");
    fgets(str, 512, fTsk); sscanf(str, "%s", strSrc); DOS_PATH(strSrc);
    fgets(str, 512, fTsk); sscanf(str, "%d%lf%lf%lf%lf%lf%lf%lf%d%d%d%d%d%d%d%d%d%d", &idx, &cx, &cy, &cz, &phi, &img, &kap, &grdZ, &yy, &mm, &dd, &ho, &mi, &se, &utmZn, &gs, &ws, &bTxt);

    CWuErsImage domImg, basImg; strcat(strSrc, ".ers");
    if (!domImg.Load4File(strSrc)) return FALSE;
    int cols = domImg.GetCols();
    int rows = domImg.GetRows();
    int bnds = domImg.GetBands();
    int pxSz = domImg.PxByte(domImg.GetPxType()) * bnds;
    double gx, gy, gz, lon, lat, hei; short cv[4], rv[4];
    double avK1 = 0, avK2 = 0, ks = 0; int r, c, rowsr; CWuGeoCvt geoCvt;
    geoCvt.Set_Cvt_Par(ET_WGS84, UTM_PROJECTION, SEMIMAJOR_WGS84, SEMIMINOR_WGS84, 0, Zone2CenterMerdian(utmZn) * SPGC_D2R, 500000, 0, 0.9996, 0);

    ////// Cal avK1 avK2
    strcpy(str, strTsk); strcat(str, "_skm.txt");
    FILE* fKM = fopen(str, "wt");
    if (fKM) {
        for (r = 1; r < rows - gs; r += gs) {
            for (c = 1; c < cols - gs; c += gs) {
                short* pC = (short*)(domImg.m_pImgDat + (r * cols + c) * pxSz);
                if (pC[0] == 0 && pC[1] == 0 && pC[2] == 0) continue;

                gx = domImg.m_tlX + c * domImg.m_dx;
                gy = domImg.m_tlY - (rows - 1 - r) * domImg.m_dy;
                gz = grdZ;
                geoCvt.Cvt_Prj2LBH(gx, gy, gz, &lon, &lat, &hei);
                getSunPos(gx, gy, gz, cx, cy, cz, lon * SPGC_R2D, lat * SPGC_R2D, yy, mm, dd, ho, mi, se, &sz, &vz, &as);
                avK1 += getKval(1, sz, vz, as);
                avK2 += getKval(4, sz, vz, as);
                ks += 1;
            }
        }
        avK1 /= ks; avK2 /= ks;
        cprintf("avK1 = %lf avK2 = %lf\n", avK1, avK2);

        fprintf(fKM, "%lf %lf \n", avK1, avK2);
        fclose(fKM);
    }

    COlpFile olpF;
    while (!feof(fTsk)) {
        if (!fgets(str, 512, fTsk)) break;
        sscanf(str, "%s", strRef);
        if (!fgets(str, 512, fTsk)) break;
        if (sscanf(str, "%d%lf%lf%lf%lf%lf%lf%lf%d%d%d%d%d%d", &idx, &cx1, &cy1, &cz1, &phi1, &img1, &kap1, &grdZ1, &yy1, &mm1, &dd1, &ho1, &mi1, &se1) < 10) break;

        DOS_PATH(strRef); strcpy(strOlp, strTsk);  strcpy(strrchr(strOlp, '.'), "_"); strcat(strOlp, strrchr(strRef, '\\') + 1); strcat(strOlp, ".olp");  cprintf("%s\n", strOlp);
        strcat(strRef, ".ers"); if (!basImg.Load4File(strRef)) continue;

        char strPlot[512]; strcpy(strPlot, strOlp);
        char* pExtPng = strrchr(strPlot, '.'); if (pExtPng) strcpy(pExtPng, ".png"); else strcat(strPlot, ".png");
        int tieSum = 0, vSum = 0, cSum = 0;
        //bool usedExistingOlp = false;
        //std::vector<cv::Point2f> plotDom, plotRef; // 非现有 .olp 流程用于绘图

        // 先尝试使用现有 .olp
        //if (IsExist(strOlp) && olpF.Load4File(strOlp)) {
        //    int oz = 0; olpF.GetData(&oz);
        //    tieSum = oz;
        //    usedExistingOlp = true;
        //    cprintf("Use existing OLP: %s tieSum=%d\n", strOlp, tieSum);
        //}
        //else {
            // 无 .olp，执行匹配生成
            olpF.SetSize(0); rowsr = basImg.GetRows();

#ifdef USE_OPENCV_MATCH
            // 计算重叠掩膜（像素域）
            cv::Mat maskDom, maskRef;
            // 仅当非绝对基准时才尝试 ORB；绝对基准用地理坐标采样
            if (idx == -1) {
                // —— 绝对基准：用地理坐标采样（复用非OpenCV分支的逻辑）——
                rowsr = basImg.GetRows();
                for (r = 1; r < rows - gs; r += gs) {
                    for (c = 1; c < cols - gs; c += gs) {
                        short* pC = (short*)(domImg.m_pImgDat + (r * cols + c) * pxSz);
                        if (pC[0] == 0 && pC[1] == 0 && pC[2] == 0) continue;

                        gx = domImg.m_tlX + c * domImg.m_dx;
                        gy = domImg.m_tlY - (rows - 1 - r) * domImg.m_dy;
                        gz = grdZ;
                        if (!GetPxl(gx, gy, &domImg, cv, ws, &fc, &fr)) continue;
                        if (cv[0] == 0 && cv[1] == 0 && cv[2] == 0) continue;
                        if (!GetPxl(gx, gy, &basImg, rv, /*绝对基准时*/0, &fc1, &fr1)) continue;
                        if (rv[0] == 0 && rv[1] == 0 && rv[2] == 0) continue;
                        if (rv[0] < 0 || rv[1] < 0 || rv[2] < 0 || rv[3] < 0 || cv[0] < 0 || cv[1] < 0 || cv[2] < 0 || cv[3] < 0) {
                            vSum++;
                            continue;
                        }
                        //double corr = CalculateRadiationCorrelation(cv, rv);
                        //if (corr < minCorr) {
                        //    cSum++;
                        //    continue;
                        //}

                        geoCvt.Cvt_Prj2LBH(gx, gy, gz, &lon, &lat, &hei);
                        getSunPos(gx, gy, gz, cx, cy, cz, lon * SPGC_R2D, lat * SPGC_R2D, yy, mm, dd, ho, mi, se, &sz, &vz, &as);
                        if (yy1 == 0) { sz1 = vz1 = as1 = 0; }
                        else getSunPos(gx, gy, gz, cx1, cy1, cz1, lon * SPGC_R2D, lat * SPGC_R2D, yy1, mm1, dd1, ho1, mi1, se1, &sz1, &vz1, &as1);

                        tieSum++;
                        olpF.Append(sz, vz, as, int(fc), int(rows - 1 - fr), cv, sz1, vz1, as1, int(fc1), int(rowsr - 1 - fr1), rv);

                        // 为可视化保存像素坐标（顶->下坐标系，绘图函数已做GSD归一）
                        //plotDom.emplace_back((float)fc, (float)fr);
                        //plotRef.emplace_back((float)fc1, (float)fr1);
                    }
                }
                cprintf("Geo tieSum=%d, skip(value<=0)=%d corrSkip=%d\n", tieSum, vSum, cSum);
            }
            else {
                // —— 非绝对基准：用 ORB 多尺度 + 掩膜 —— 
                if (!BuildOverlapMasks(domImg, basImg, maskDom, maskRef, /*pad=*/128)) {
                    cprintf("No overlap, skip pair.\n");
                    continue;
                }

                ORBMatchResult fm = RunORBMatch(domImg, basImg,
                    /*maxFeatures*/40000, /*scaleFactor*/1.2f,
                    /*nLevels*/8, /*ratio*/0.80f,
                    /*ransacThresh*/3.0, /*ransacConf*/0.999,
                    /*maskDom*/ &maskDom, /*maskRef*/ &maskRef);
                cprintf("ORB inliers = %zu\n", fm.ptsDom.size());

                //plotDom.reserve(fm.ptsDom.size()); plotRef.reserve(fm.ptsRef.size());

                // 生成 .olp
                for (size_t k = 0; k < fm.ptsDom.size(); ++k) {
                    float fc = fm.ptsDom[k].x;
                    float fr = fm.ptsDom[k].y;
                    float fc1 = fm.ptsRef[k].x;
                    float fr1 = fm.ptsRef[k].y;

                    if (fc < 0 || fr < 0 || fc >= cols || fr >= rows) continue;
                    if (fc1 < 0 || fr1 < 0 || fc1 >= basImg.GetCols() || fr1 >= rowsr) continue;

                    short cv[4] = { 0,0,0,0 }, rv[4] = { 0,0,0,0 };
                    const WORD* pD = (const WORD*)domImg.m_pImgDat + (INT64)int(fr) * cols * bnds + int(fc) * bnds;
                    const WORD* pR = (const WORD*)basImg.m_pImgDat + (INT64)int(fr1) * basImg.GetCols() * bnds + int(fc1) * bnds;
                    for (int b = 0; b < bnds && b < 4; b++) { cv[b] = (short)pD[b]; rv[b] = (short)pR[b]; }
                    if (cv[0] == 0 && cv[1] == 0 && cv[2] == 0) continue;
                    if (rv[0] == 0 && rv[1] == 0 && rv[2] == 0) continue;
                    if (rv[0] < 0 || rv[1] < 0 || rv[2] < 0 || rv[3] < 0 || cv[0] < 0 || cv[1] < 0 || cv[2] < 0 || cv[3] < 0) {
                        continue;
                    }
                    gx = domImg.m_tlX + fc * domImg.m_dx;
                    gy = domImg.m_tlY - (rows - 1 - fr) * domImg.m_dy;
                    gz = grdZ;
                    geoCvt.Cvt_Prj2LBH(gx, gy, gz, &lon, &lat, &hei);
                    getSunPos(gx, gy, gz, cx, cy, cz, lon * SPGC_R2D, lat * SPGC_R2D, yy, mm, dd, ho, mi, se, &sz, &vz, &as);
                    if (yy1 == 0) { sz1 = vz1 = as1 = 0; }
                    else getSunPos(gx, gy, gz, cx1, cy1, cz1, lon * SPGC_R2D, lat * SPGC_R2D, yy1, mm1, dd1, ho1, mi1, se1, &sz1, &vz1, &as1);

                    olpF.Append(sz, vz, as,
                        int(fc), int(rows - 1 - fr), cv,
                        sz1, vz1, as1,
                        int(fc1), int(rowsr - 1 - fr1), rv);
                    //plotDom.emplace_back(fc, fr);
                    //plotRef.emplace_back(fc1, fr1);
                    tieSum++;
                }
                cprintf("ORB tieSum=%d\n", tieSum);
            }
#else
            for (r = 1; r < rows - gs; r += gs) {
                for (c = 1; c < cols - gs; c += gs) {
                    short* pC = (short*)(domImg.m_pImgDat + (r * cols + c) * pxSz);
                    if (pC[0] == 0 && pC[1] == 0 && pC[2] == 0) continue;

                    gx = domImg.m_tlX + c * domImg.m_dx;
                    gy = domImg.m_tlY - (rows - 1 - r) * domImg.m_dy;
                    gz = grdZ;
                    if (!GetPxl(gx, gy, &domImg, cv, ws, &fc, &fr)) continue;
                    if (cv[0] == 0 && cv[1] == 0 && cv[2] == 0) continue;
                    if (!GetPxl(gx, gy, &basImg, rv, (yy1 == 0) ? 0 : ws, &fc1, &fr1)) continue;
                    if (rv[0] == 0 && rv[1] == 0 && rv[2] == 0) continue;
                    if (rv[0] < 0 || rv[1] < 0 || rv[2] < 0 || rv[3] < 0 || cv[0] < 0 || cv[1] < 0 || cv[2] < 0 || cv[3] < 0) {
                        vSum++;
                        continue;
                    }
                    double corr = CalculateRadiationCorrelation(cv, rv);
                    if (corr < minCorr) {
                        cSum++;
                        continue;
                    }
                    geoCvt.Cvt_Prj2LBH(gx, gy, gz, &lon, &lat, &hei);
                    getSunPos(gx, gy, gz, cx, cy, cz, lon * SPGC_R2D, lat * SPGC_R2D, yy, mm, dd, ho, mi, se, &sz, &vz, &as);
                    if (yy1 == 0) { sz1 = vz1 = as1 = 0; }
                    else getSunPos(gx, gy, gz, cx1, cy1, cz1, lon * SPGC_R2D, lat * SPGC_R2D, yy1, mm1, dd1, ho1, mi1, se1, &sz1, &vz1, &as1);
                    tieSum++;
                    olpF.Append(sz, vz, as, int(fc), int(rows - 1 - fr), cv, sz1, vz1, as1, int(fc1), int(rowsr - 1 - fr1), rv);

                }
            }
            cprintf("tieSum= %d, skip (value<=0)=%d correlation=%d\n", tieSum, vSum, cSum);
#endif
        //}
        if (olpF.GetSize() > 0) {
            olpF.Save2File(strOlp);
            if (bTxt) {
                char strTxt[512]; strcpy(strTxt, strOlp); strcat(strTxt, ".txt");
                olpF.Save2File(strTxt, FALSE);
            }
//#ifdef USE_OPENCV_MATCH
//            if (usedExistingOlp) {
//                SaveMatchesPlotFromOlp(domImg, basImg, olpF, strPlot);
//            }
//            else if (!plotDom.empty()) {
//                SaveMatchesPlotVec(domImg, basImg, plotDom, plotRef, strPlot);
//            }
//#endif
        }
    }
    fclose(fTsk);

    return 0;
}
