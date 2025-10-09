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
#include <opencv2/features2d.hpp>
#pragma comment(lib,"opencv_imgproc490d.lib")
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
    
    char strRom[256]; sprintf( strRom,"%s\\ROM_S2Bexp",strDir );
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

        double* aa = new double[(sum * 4) * (sum * 4)];
        double* b = new double[(sum * 4)];
        double* a = new double[(sum * 4)];
        double* x = new double[(sum * 4)];
        double* xPrev = new double[(sum * 4)];

        // 稳健IRLS参数
        const int ROBUST_MAX_ITERS = 1;     // 稳健迭代最多轮次（轻量）
        const double HUBER_C = 1.345;       // Huber常数（单位为sigma倍数）
        auto huberW = [](double r, double k) -> double {
            double ar = fabs(r);
            if (k <= 1e-12) return 1.0;
            return (ar <= k) ? 1.0 : (k / ar);
            };

        ProgBegin(sum * 4);
        for (c = 0; c < 4; c++) {
            UINT st = GetTickCount(); print2Log("proc band %d , Norml ... st= %d\n", c + 1, st);

            // 初始法方程（保持原基线权）
            memset(aa, 0, sizeof(double) * (sum * 4) * (sum * 4));
            memset(b, 0, sizeof(double) * (sum * 4));
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
                        if (idxr == -1) {
                            for (v = 0; v < oz; v++, pOs++) {
                                k1 = getKval(1, pOs->csz, pOs->cvz, pOs->cas) - pAK1[idx];
                                k2 = getKval(4, pOs->csz, pOs->cvz, pOs->cas) - pAK2[idx];
                                memset(a, 0, sizeof(double) * (sum * 4));
                                l = (pOs->rv[c]);
                                a[idx * 4 + 0] = -1;
                                a[idx * 4 + 1] = -k1;
                                a[idx * 4 + 2] = -k2;
                                a[idx * 4 + 3] = pOs->cv[c];
                                Nrml(a, (sum * 4), l, aa, b, 1.0);
                            }
                        }
                        else {
                            for (v = 0; v < oz; v++, pOs++) {
                                k1 = getKval(1, pOs->csz, pOs->cvz, pOs->cas) - pAK1[idx];
                                k2 = getKval(4, pOs->csz, pOs->cvz, pOs->cas) - pAK2[idx];
                                k1r = getKval(1, pOs->rsz, pOs->rvz, pOs->ras) - pAK1[idxr];
                                k2r = getKval(4, pOs->rsz, pOs->rvz, pOs->ras) - pAK2[idxr];
                                memset(a, 0, sizeof(double) * (sum * 4));
                                l = pOs->cv[c] - pOs->rv[c];
                                a[idx * 4 + 0] = 1;  a[idx * 4 + 1] = k1;  a[idx * 4 + 2] = k2;
                                a[idxr * 4 + 0] = -1; a[idxr * 4 + 1] = -k1r; a[idxr * 4 + 2] = -k2r;
                                Nrml(a, (sum * 4), l, aa, b, 0.1);
                            }
                        }
                    }
                }
                fclose(fTsk);
                print2Log(", used tm= %.2lf sec \n", (GetTickCount() - ss) * 0.001);
            }
            print2Log("Norml ...over. tm= %.2lf sec \nSolve ... st= %d \n", (GetTickCount() - st) * 0.001, GetTickCount());

            // 初解
            memset(x, 0, sizeof(double) * (sum * 4));
            Solve(aa, b, x, (sum * 4), (sum * 4));
            memcpy(xPrev, x, sizeof(double) * (sum * 4));
            double sigmaForK_prev = 0.0;
            // 稳健IRLS
            for (int it = 0; it < ROBUST_MAX_ITERS; ++it) {
                UINT stRes = GetTickCount();
                // 1) 计算残差RMS（近似sigma）
                double sumr2 = 0.0; INT64 nobs = 0;

                for (i = 0; i < sum; i++) {
                    m_listCtrl.GetItemText(i, 0, str, 256);
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
                        if (!olpF.Load4File(strOlp)) continue;

                        int oz = 0; OBV* pOs = olpF.GetData(&oz);
                        for (int v = 0; v < oz; ++v, ++pOs) {
                            double k1, k2, k1r = 0, k2r = 0, l;
                            if (idxr == -1) {
                                k1 = getKval(1, pOs->csz, pOs->cvz, pOs->cas) - pAK1[idx];
                                k2 = getKval(4, pOs->csz, pOs->cvz, pOs->cas) - pAK2[idx];
                                l = pOs->rv[c];
                                // dot = a*x
                                double dot = (-1.0) * x[idx * 4 + 0]
                                    + (-k1) * x[idx * 4 + 1]
                                    + (-k2) * x[idx * 4 + 2]
                                    + (double)pOs->cv[c] * x[idx * 4 + 3];
                                double r = l - dot;
                                sumr2 += r * r; nobs++;
                            }
                            else {
                                k1 = getKval(1, pOs->csz, pOs->cvz, pOs->cas) - pAK1[idx];
                                k2 = getKval(4, pOs->csz, pOs->cvz, pOs->cas) - pAK2[idx];
                                k1r = getKval(1, pOs->rsz, pOs->rvz, pOs->ras) - pAK1[idxr];
                                k2r = getKval(4, pOs->rsz, pOs->rvz, pOs->ras) - pAK2[idxr];
                                l = pOs->cv[c] - pOs->rv[c];
                                double dot =
                                    (1.0) * x[idx * 4 + 0] + (k1)*x[idx * 4 + 1] + (k2)*x[idx * 4 + 2]
                                    + (-1.0) * x[idxr * 4 + 0] + (-k1r) * x[idxr * 4 + 1] + (-k2r) * x[idxr * 4 + 2];
                                double r = l - dot;
                                sumr2 += r * r; nobs++;
                            }
                        }
                    }
                    fclose(fTsk);
                }
                double dof = (double)std::max<INT64>(1, nobs - (INT64)sum * 4);
                double sigmaRaw = std::sqrt(sumr2 / dof);

                // 用于门限的尺度：单调不升，避免阈值变宽
                double sigmaForK = (it == 0) ? sigmaRaw : std::min(sigmaForK_prev, sigmaRaw);
                sigmaForK_prev = sigmaForK;
                double kThr = HUBER_C * sigmaForK;
                print2Log("band %d iter %d: nobs=%lld sigmaRaw=%.6f sigmaForK=%.6f (tm=%.2fs)\n",
                    c + 1, it + 1, (long long)nobs, sigmaRaw, sigmaForK, (GetTickCount() - stRes) * 0.001);

                // 2) 以Huber权重重建法方程
                memset(aa, 0, sizeof(double) * (sum * 4) * (sum * 4));
                memset(b, 0, sizeof(double) * (sum * 4));
                double sumr2W = 0.0, sumW = 0.0;
                for (i = 0; i < sum; i++) {
                    m_listCtrl.GetItemText(i, 0, str, 256);
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
                        if (!olpF.Load4File(strOlp)) continue;

                        int oz = 0; OBV* pOs = olpF.GetData(&oz);
                        if (idxr == -1) {
                            for (int v = 0; v < oz; ++v, ++pOs) {
                                double k1 = getKval(1, pOs->csz, pOs->cvz, pOs->cas) - pAK1[idx];
                                double k2 = getKval(4, pOs->csz, pOs->cvz, pOs->cas) - pAK2[idx];
                                double l = (pOs->rv[c]);
                                double dot = (-1.0) * x[idx * 4 + 0]
                                    + (-k1) * x[idx * 4 + 1]
                                    + (-k2) * x[idx * 4 + 2]
                                    + (double)pOs->cv[c] * x[idx * 4 + 3];
                                double r = l - dot;
                                double w = 1.0 * huberW(r, kThr); // 基线权(1.0) * 残差权

                                memset(a, 0, sizeof(double) * (sum * 4));
                                a[idx * 4 + 0] = -1;
                                a[idx * 4 + 1] = -k1;
                                a[idx * 4 + 2] = -k2;
                                a[idx * 4 + 3] = pOs->cv[c];
                                Nrml(a, (sum * 4), l, aa, b, w);

                                sumr2W += w * r * r; sumW += w;
                            }
                        }
                        else {
                            for (int v = 0; v < oz; ++v, ++pOs) {
                                double k1 = getKval(1, pOs->csz, pOs->cvz, pOs->cas) - pAK1[idx];
                                double k2 = getKval(4, pOs->csz, pOs->cvz, pOs->cas) - pAK2[idx];
                                double k1r = getKval(1, pOs->rsz, pOs->rvz, pOs->ras) - pAK1[idxr];
                                double k2r = getKval(4, pOs->rsz, pOs->rvz, pOs->ras) - pAK2[idxr];
                                double l = pOs->cv[c] - pOs->rv[c];
                                double dot =
                                    (1.0) * x[idx * 4 + 0] + (k1)*x[idx * 4 + 1] + (k2)*x[idx * 4 + 2]
                                    + (-1.0) * x[idxr * 4 + 0] + (-k1r) * x[idxr * 4 + 1] + (-k2r) * x[idxr * 4 + 2];
                                double r = l - dot;
                                double w = 0.1 * huberW(r, kThr); // 基线权(0.05) * 残差权

                                memset(a, 0, sizeof(double) * (sum * 4));
                                a[idx * 4 + 0] = 1;  a[idx * 4 + 1] = k1;  a[idx * 4 + 2] = k2;
                                a[idxr * 4 + 0] = -1; a[idxr * 4 + 1] = -k1r; a[idxr * 4 + 2] = -k2r;
                                Nrml(a, (sum * 4), l, aa, b, w);

                                sumr2W += w * r * r; sumW += w;
                            }
                        }
                    }
                    fclose(fTsk);
                }

                // 3) 重新求解并检查收敛
                Solve(aa, b, x, (sum * 4), (sum * 4));
                double dofW = std::max(1.0, sumW - (double)sum * 4);
                double sigmaW = std::sqrt(std::max(0.0, sumr2W / dofW));
                print2Log("band %d iter %d solved. sigmaW=%.6f (weighted), sumW=%.3f, dofW=%.0f\n",
                    c + 1, it + 1, sigmaW, sumW, dofW);

                double maxDiff = 0.0;
                for (int q = 0; q < sum * 4; ++q) {
                    maxDiff = std::max(maxDiff, std::fabs(x[q] - xPrev[q]));
                }
                print2Log("band %d iter %d solved. max |dx| = %.3e\n", c + 1, it + 1, maxDiff);
                memcpy(xPrev, x, sizeof(double) * (sum * 4));
                if (maxDiff < 1e-6) { print2Log("band %d converged at iter %d\n", c + 1, it + 1); break; }
            } // end IRLS

            // 输出最终解
            sprintf(str, "%s//pre_bnd%d.txt", strRom, c + 1);
            FILE* fKnl = fopen(str, "wt");
            for (i = 0; i < sum; i++) {
                m_listCtrl.GetItemText(i, 0, str, 256);
                fprintf(fKnl, "%9.6lf \t %15.6lf \t %15.6lf \t %15.6lf \t %s\n",
                    x[i * 4 + 3], x[i * 4 + 0], x[i * 4 + 1], x[i * 4 + 2], strrchr(str, '\\'));
            }
            fclose(fKnl);
        }
        delete[]pAK1;
        delete[]aa;
        delete[]a;
        delete[]b;
        delete[]x;
        delete[]xPrev;

        ProgEnd();
        print2Log("RadBA over.\n");
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

// 从16位多光谱取灰度（前3波段均值）
static inline double GrayFromWordBands(const WORD* px) {
    int valid = 0; int sum = 0;
    for (int b = 0; b < 3; ++b) { if (px[b] > 0) { sum += (int)px[b]; valid++; } }
    return valid ? double(sum) / valid : 0.0;
}

// 计算ZNCC
static inline double ZNCC(const std::vector<double>& A, double meanA, double stdA,
    const std::vector<double>& B, double meanB, double stdB)
{
    if (A.size() != B.size() || stdA < 1e-9 || stdB < 1e-9) return -1.0;
    double s = 0.0;
    for (size_t i = 0; i < A.size(); ++i) s += (A[i] - meanA) * (B[i] - meanB);
    return s / (double(A.size()) * stdA * stdB);
}

// 验证几何位置对应的 ZNCC（基准影像块中心(cxBase,cyBase)，半径rad，ZNCC阈值znccThr）
static bool VerifyByZNCC(CWuErsImage& baseImg, CWuErsImage& hrImg,
    int cxBase, int cyBase, int rad, double znccThr)
{
    const int colsB = baseImg.GetCols(), rowsB = baseImg.GetRows();
    if (cxBase - rad < 0 || cyBase - rad < 0 || cxBase + rad >= colsB || cyBase + rad >= rowsB)
        return false;

    int avzX = (int)std::round(std::abs(baseImg.m_dx / hrImg.m_dx));
    int avzY = (int)std::round(std::abs(baseImg.m_dy / hrImg.m_dy));
    int avz = std::max(1, (int)std::round(0.5 * (avzX + avzY)));//50

    const WORD* basePtr = (const WORD*)baseImg.m_pImgDat;
    const int side = 2 * rad + 1;

    std::vector<double> basePatch; basePatch.reserve(side * side);
    std::vector<double> hrPatch;   hrPatch.reserve(side * side);

    double sB = 0.0, sB2 = 0.0, sH = 0.0, sH2 = 0.0;
    int validBoth = 0;

    for (int dy = -rad; dy <= rad; ++dy) {
        int yB = cyBase + dy;
        const WORD* rowPtrB = basePtr + (INT64)yB * colsB * baseImg.GetBands();
        for (int dx = -rad; dx <= rad; ++dx) {
            int xB = cxBase + dx;
            const WORD* pxB = rowPtrB + xB * baseImg.GetBands();

            double gB = GrayFromWordBands(pxB);
            basePatch.push_back(gB);

            double gx = baseImg.m_tlX + xB * baseImg.m_dx;
            double gy = baseImg.m_tlY - (rowsB - 1 - yB) * baseImg.m_dy;

            short pv[4] = { 0,0,0,0 }; double fcx, fcy;
            GetPxl(gx, gy, &hrImg, pv, avz, &fcx, &fcy);
            double gH = GrayFromWordBands((const WORD*)pv);
            hrPatch.push_back(gH);

            if (gB > 0 && gH > 0) {
                sB += gB; sB2 += gB * gB;
                sH += gH; sH2 += gH * gH;
                validBoth++;
            }
        }
    }

    if (validBoth < side * side * 7 / 10) return false;

    double mB = sB / validBoth, mH = sH / validBoth;
    double vB = (sB2 - sB * sB / validBoth), vH = (sH2 - sH * sH / validBoth);
    double sBstd = (validBoth > 1 && vB > 0) ? std::sqrt(vB / (validBoth - 1)) : 0.0;
    double sHstd = (validBoth > 1 && vH > 0) ? std::sqrt(vH / (validBoth - 1)) : 0.0;
    if (sBstd < RAD_MIN_STD || sHstd < RAD_MIN_STD) return false;

    double zn = ZNCC(basePatch, mB, sBstd, hrPatch, mH, sHstd);
    return zn >= znccThr;
}
// 计算将 img 对齐到 coarse 的块平均窗口（>=2 则返回窗口尺寸，否则返回 0 表示单像元/双线性）
static inline int CalcAvzToCoarse(const CWuErsImage& coarse, const CWuErsImage& img) {
    double rx = std::abs(coarse.m_dx / img.m_dx);
    double ry = std::abs(coarse.m_dy / img.m_dy);
    if (!std::isfinite(rx) || rx <= 0) rx = 1.0;
    if (!std::isfinite(ry) || ry <= 0) ry = 1.0;
    int avz = (int)std::round(0.5 * (rx + ry));
    return (avz >= 2) ? avz : 0;
}

// 并行收集结果的记录
struct RadZNCCRec {
    int cxDom, cyDom;     // 主影像像素（顶->下）
    int cxRef, cyRef;     // 参考影像像素（顶->下）
    short cv[4], rv[4];   // 主/参考 4波段
    double sz, vz, as;    // 主影像太阳参数
    double sz1, vz1, as1; // 参考影像太阳参数
};
//////////////////////////////////////////////////////////////////////////////////
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
    int oz = 0; const OBV* pData = olp.GetData(&oz);
    if (!pData || oz <= 0) return;

    cv::Mat gL0, gR0;
    WuErsToGray8(domImg, gL0);
    WuErsToGray8(refImg, gR0);

    // 基于像元大小做显示尺度归一：统一到较粗分辨率
    const double gsdL = std::abs(domImg.m_dx);
    const double gsdR = std::abs(refImg.m_dx);
    const double gsdCoarse = std::max(gsdL, gsdR);
    double sL = (gsdL > 0) ? (gsdL / gsdCoarse) : 1.0; // <=1
    double sR = (gsdR > 0) ? (gsdR / gsdCoarse) : 1.0; // <=1
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

    // 计算每个点矩形框的半宽（画布坐标，已考虑缩放）
    int halfBoxL = 6;
    int halfBoxR = 6;
    double minXL = 1e30, minYL = 1e30, maxXL = -1e30, maxYL = -1e30;
    double minXR = 1e30, minYR = 1e30, maxXR = -1e30, maxYR = -1e30;
    {
        const OBV* pAll = olp.GetData(&oz);
        for (int i = 0; i < oz; ++i) {
            const OBV& o = pAll[i];
            double rL_top = (gL0.rows - 1 - o.cr) * sL;
            double rR_top = (gR0.rows - 1 - o.rr) * sR;
            double cL_s = o.cc * sL;
            double cR_s = o.rc * sR;

            if (cL_s >= 0 && rL_top >= 0 && cL_s < WL && rL_top < gL.rows) {
                minXL = std::min(minXL, cL_s);
                minYL = std::min(minYL, rL_top);
                maxXL = std::max(maxXL, cL_s);
                maxYL = std::max(maxYL, rL_top);
            }
            if (cR_s >= 0 && rR_top >= 0 && cR_s < WR && rR_top < gR.rows) {
                minXR = std::min(minXR, cR_s);
                minYR = std::min(minYR, rR_top);
                maxXR = std::max(maxXR, cR_s);
                maxYR = std::max(maxYR, rR_top);
            }
        }
        // 画全局包围盒（左右图分别）
        if (minXL < maxXL && minYL < maxYL) {
            cv::Rect rL((int)std::floor(std::max(0.0, minXL - halfBoxL)),
                (int)std::floor(std::max(0.0, minYL - halfBoxL)),
                (int)std::ceil(std::min<double>(WL - 1, maxXL + halfBoxL)) - (int)std::floor(std::max(0.0, minXL - halfBoxL)),
                (int)std::ceil(std::min<double>(gL.rows - 1, maxYL + halfBoxL)) - (int)std::floor(std::max(0.0, minYL - halfBoxL)));
            rL &= cv::Rect(0, 0, WL, gL.rows);
            cv::rectangle(canvas, rL, cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
        }
        if (minXR < maxXR && minYR < maxYR) {
            cv::Rect rR((int)std::floor(std::max(0.0, minXR - halfBoxR)) + WL,
                (int)std::floor(std::max(0.0, minYR - halfBoxR)),
                (int)std::ceil(std::min<double>(WR - 1, maxXR + halfBoxR)) - (int)std::floor(std::max(0.0, minXR - halfBoxR)),
                (int)std::ceil(std::min<double>(gR.rows - 1, maxYR + halfBoxR)) - (int)std::floor(std::max(0.0, minYR - halfBoxR)));
            rR &= cv::Rect(WL, 0, WR, gR.rows);
            cv::rectangle(canvas, rR, cv::Scalar(255, 0, 0), 2, cv::LINE_AA);
        }
    }
    // 逐点绘制
    cv::RNG rng(12345);
    const OBV* p = pData;
    for (int i = 0; i < oz; i += 10, p += 10) { // 仍旧抽样绘制，避免过密
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

/////////////////////////////////////////////////////////////////////////////
BOOL MchTie(LPCSTR lpstrPar)
{
    char strSrc[256], strRef[256], strTsk[256], str[512], strOlp[512];
    double fc, fr, fc1, fr1, sz, vz, as, sz1, vz1, as1;
    double cx, cy, cz, phi, img, kap, grdZ, cx1, cy1, cz1, phi1, img1, kap1, grdZ1;
    int idx, yy, mm, dd, ho, mi, se, yy1, mm1, dd1, ho1, mi1, se1, utmZn = 0, gs = 21, ws = 5, bTxt = 0;

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

        // 现有 .olp
        if (/*IsExist(strOlp) && olpF.Load4File(strOlp)*/FALSE) {
            SaveMatchesPlotFromOlp(domImg, basImg, olpF, strPlot);
        }
        else {
            // 无 .olp，执行匹配生成
            olpF.SetSize(0); rowsr = basImg.GetRows();

#ifdef USE_MY_MATCH
            const int rowsD = domImg.GetRows();
            const int colsD = domImg.GetCols();

            // 自动选择较粗影像为基准尺度（粗影像直接取像元，细影像用块平均）
            const double gsdDom = std::abs(domImg.m_dx);
            const double gsdRef = std::abs(basImg.m_dx);
            const bool domIsCoarse = (gsdDom > gsdRef);
            const bool sameRes = (gsdDom == gsdRef);
            const CWuErsImage& coarseImg = domIsCoarse ? domImg : basImg;

            const int avzDom = sameRes ? 0 : CalcAvzToCoarse(coarseImg, domImg);
            const int avzRef = sameRes ? 0 : CalcAvzToCoarse(coarseImg, basImg);

            std::vector<RadZNCCRec> allRecs;
            allRecs.reserve(((rowsD / gs) + 1) * ((colsD / gs) + 1) / 2);

#ifdef _OPENMP
#pragma omp parallel
#endif
            {
                std::vector<RadZNCCRec> localRecs;
                localRecs.reserve(2048);
                int vLocal = 0, cLocal = 0;

#ifdef _OPENMP
#pragma omp for schedule(dynamic, 32) nowait
#endif
                for (int rr = 1; rr < rowsD - gs; rr += gs) {
                    for (int cc = 1; cc < colsD - gs; cc += gs) {
                        double gx_l = domImg.m_tlX + cc * domImg.m_dx;
                        double gy_l = domImg.m_tlY - (rowsD - 1 - rr) * domImg.m_dy;
                        double gz_l = grdZ;

                        short cv_l[4], rv_l[4];
                        double fc_l, fr_l, fc1_l, fr1_l;
                        if (!GetPxl(gx_l, gy_l, &domImg, cv_l, avzDom, &fc_l, &fr_l)) continue;
                        if (cv_l[0] == 0 && cv_l[1] == 0 && cv_l[2] == 0) continue;
                        if (!GetPxl(gx_l, gy_l, &basImg, rv_l, avzRef, &fc1_l, &fr1_l)) continue;
                        if (rv_l[0] == 0 && rv_l[1] == 0 && rv_l[2] == 0) continue;
                        if (rv_l[0] < 0 || rv_l[1] < 0 || rv_l[2] < 0 || rv_l[3] < 0 ||
                            cv_l[0] < 0 || cv_l[1] < 0 || cv_l[2] < 0 || cv_l[3] < 0) {
                            vLocal++; continue;
                        }
                        // ZNCC 验证
                        int verifyR = std::max(3, RAD_PATCH_RADIUS);
                        bool ok = false;
                        if (domIsCoarse) {
                            int cxBase = (int)std::round(fc_l);
                            int cyBase = (int)std::round(fr_l);
                            ok = VerifyByZNCC(domImg, basImg, cxBase, cyBase, verifyR, RAD_MIN_ZNCC);
                        }
                        else {
                            int cxBase = (int)std::round(fc1_l);
                            int cyBase = (int)std::round(fr1_l);
                            ok = VerifyByZNCC(basImg, domImg, cxBase, cyBase, verifyR, RAD_MIN_ZNCC);
                        }
                        if (!ok) { cLocal++; continue; }

                        // 太阳参数（逐点独立）
                        double lon_l, lat_l, hei_l, sz_l, vz_l, as_l, sz1_l = 0, vz1_l = 0, as1_l = 0;
                        geoCvt.Cvt_Prj2LBH(gx_l, gy_l, gz_l, &lon_l, &lat_l, &hei_l);
                        getSunPos(gx_l, gy_l, gz_l, cx, cy, cz,
                            lon_l * SPGC_R2D, lat_l * SPGC_R2D,
                            yy, mm, dd, ho, mi, se,
                            &sz_l, &vz_l, &as_l);
                        if (yy1 != 0) {
                            getSunPos(gx_l, gy_l, gz_l, cx1, cy1, cz1,
                                lon_l * SPGC_R2D, lat_l * SPGC_R2D,
                                yy1, mm1, dd1, ho1, mi1, se1,
                                &sz1_l, &vz1_l, &as1_l);
                        }

                        // 收集
                        RadZNCCRec rec{};
                        rec.cxDom = (int)std::round(fc_l);
                        rec.cyDom = (int)std::round(fr_l);
                        rec.cxRef = (int)std::round(fc1_l);
                        rec.cyRef = (int)std::round(fr1_l);
                        rec.cv[0] = cv_l[0]; rec.cv[1] = cv_l[1]; rec.cv[2] = cv_l[2]; rec.cv[3] = cv_l[3];
                        rec.rv[0] = rv_l[0]; rec.rv[1] = rv_l[1]; rec.rv[2] = rv_l[2]; rec.rv[3] = rv_l[3];
                        rec.sz = sz_l; rec.vz = vz_l; rec.as = as_l;
                        rec.sz1 = sz1_l; rec.vz1 = vz1_l; rec.as1 = as1_l;

                        localRecs.emplace_back(rec);
                    }
                }

#ifdef _OPENMP
#pragma omp critical
#endif
                {
                    allRecs.insert(allRecs.end(), localRecs.begin(), localRecs.end());
                    vSum += vLocal; cSum += cLocal;
                }
            } // end parallel

            // 统一输出到 .olp（.olp 行号为“底->上”）
            for (const auto& rec : allRecs) {
                olpF.Append(rec.sz, rec.vz, rec.as,
                    rec.cxDom, int(rows - 1 - rec.cyDom), const_cast<short*>(rec.cv),
                    rec.sz1, rec.vz1, rec.as1,
                    rec.cxRef, int(rowsr - 1 - rec.cyRef), const_cast<short*>(rec.rv));
            }
            tieSum += (int)allRecs.size();
            cprintf("tieSum=%d (threads=%d, gs=%d, verifyR=%d, thr=%.2f) skip(value<=0)=%d verifySkip=%d\n",
                (int)allRecs.size(),
#ifdef _OPENMP
                omp_get_max_threads(),
#else
                1,
#endif
                gs, std::max(3, RAD_PATCH_RADIUS), RAD_MIN_ZNCC, vSum, cSum);
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
            if (olpF.GetSize() > 0) {
                olpF.Save2File(strOlp);
                if (bTxt) {
                    char strTxt[512]; strcpy(strTxt, strOlp); strcat(strTxt, ".txt");
                    olpF.Save2File(strTxt, FALSE);
                }
            }
        }
    }
    fclose(fTsk);

    return 0;
}
