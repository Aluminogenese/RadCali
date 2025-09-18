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
DWORD WINAPI CupThread_CRadCaliDlg( LPVOID lpParam ) 
{
    ULONG id = GetCurrentThreadId(); int tskId=0; char str[512];
    for ( int i=0;i<MAX_CPU;i++ ){ if ( gs_hThreadId[i]==id ) break; }
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


static inline char* itostr0(int num )
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
	m_strRet = _T("");
	m_strBas = _T("");
	m_gs = 8;
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
	DDX_Control(pDX, IDC_EDIT_REF, m_edtBas);
	DDX_Control(pDX, IDC_PROGRESS_CUR, m_progCur);
	DDX_Control(pDX, IDC_PROGRESS_ALL, m_progAll);
	DDX_Control(pDX, IDC_LIST_CTRL, m_listCtrl);
    DDX_Control(pDX, IDC_EDIT_RET, m_editRet);
    DDX_Text(pDX, IDC_EDIT_RET, m_strRet);
	DDX_Text(pDX, IDC_EDIT_REF, m_strBas);
	DDX_Text(pDX, IDC_EDIT_GS, m_gs);
	DDV_MinMaxInt(pDX, m_gs, 1, 999);
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

    UpdateData(FALSE);
    m_gcUsr.m_pDlg = this;
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
            while (pos != NULL){  m_listCtrl.DeleteItem( m_listCtrl.GetNextSelectedItem(pos) );
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
	pDlg->Process();  ::ExitThread( 0 );  return TRUE;
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
    BOOL bRun = FALSE; int maxTm=120,cpuSum = sysInfo.dwNumberOfProcessors-1; 
    if (cpuSum<1) cpuSum = 1;  if (cpuSum>MAX_CPU) cpuSum = MAX_CPU;
    char strCmd[1024],strExe[256]; ::GetModuleFileName(NULL,strExe,sizeof(strExe));

    char *pS,strDir[256],strT[256],str[512];
    strcpy( strDir,m_strRet ); pS = strrchr( strDir,'\\' ); if (pS) *pS=0;
    
    char strRom[256]; sprintf( strRom,"%s\\ROM_S2B",strDir );
    CreateDir( strRom );

    char strLog[256]; strcpy( strLog,m_strRet );
    sprintf( str,"%s-s2b.log",strLog ); 
    ::DeleteFile(str);  openLog(str);  

    int i,j,sum = m_listCtrl.GetItemCount();
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
    ///////////////////////////////////    
    ProgBegin(sum*2); int cancel;
    for( i=0;i<sum;i++,ProgStep(cancel) )
	{
		if ( ::WaitForSingleObject(m_hEThEHdl,1)==WAIT_OBJECT_0 ) break;

        m_listCtrl.GetItemText(i,0,str,256);
        sprintf( strT,"%s%s.tsk",strRom,strrchr(str,'\\') );
        FILE *fTsk = fopen( strT,"wt" );
        fprintf( fTsk,"%s\n%s %d\n",str,m_listCtrl.GetItemText(i,1),m_gs );
        fprintf( fTsk,"%s\n%s\n",m_strBas,"0 0 0 0 0 0 0 0 0 0 0 0 0 0 0" );

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
            fprintf( fTsk,"%s\n",m_listCtrl.GetItemText( pRi[j].idx,1 ) );
        }
        /////////////////////////////////////////////
        fclose( fTsk );

#ifdef _DEBUG
        sprintf(strCmd, "TIE@%s",strT ); 
        MchTie( strCmd ); 
        break;
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

    delete []pRi;
    delete []pRg;

    // waiting all process over in maxTm 
    stTm = CTime::GetCurrentTime();
    while( 1 ){
        for ( int c=0;c<MAX_CPU;c++ ){ if ( gs_hThreadId[c]!=NULL ) break; }
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

    /////////////////////////////////
    COlpFile olpF;
    char strSrc[256],strRef[256],strTsk[256],strOlp[512];
    for( i=0;i<sum;i++,ProgStep(cancel) ){
        if ( ::WaitForSingleObject(m_hEThEHdl,1)==WAIT_OBJECT_0 ) break;
        
        m_listCtrl.GetItemText(i,0,str,256);
        sprintf( strT,"%s%s.tsk",strRom,strrchr(str,'\\') );

        FILE *fTsk = fopen( strT,"rt" );
        fgets( str,512,fTsk ); sscanf( str,"%s",strSrc ); DOS_PATH(strSrc);
        fgets( str,512,fTsk ); 
        while( !feof(fTsk) ){
            if ( !fgets( str,512,fTsk ) ) break;
            sscanf( str,"%s", strRef ); DOS_PATH(strRef); 
            if ( !fgets( str,512,fTsk ) ) break;            
            strcpy( strOlp,strTsk );  strcpy( strrchr(strOlp,'.'),"_" ); strcat( strOlp,strrchr(strRef,'\\')+1 ); strcat( strOlp,".olp" ); 
            if ( olpF.Load4File(strOlp) ){

            }
        }
    }
    ProgEnd(); 
    
    
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

void CRadCaliDlg::OnTaskMsg( UINT tskGrpId,UINT tskId,TSKMSG tskMsg )
{
    CString strPer;  int i,tskSum = m_listCtrl.GetItemCount();
    switch( tskMsg.wParam )
    {
    case TSKPROC_START:
        for ( i=0;i<tskSum;i++ ){
            if ( int(tskGrpId)==atoi(m_listCtrl.GetItemText(i,TSK_GRP)) && int(tskId)==atoi(m_listCtrl.GetItemText(i,TSK_ID)) ){
                m_listCtrl.SetItemText( i,TSK_STA,"1 %%" );
            }
        }   
        break;
    case TSKPROC_STEP :
        for ( i=0;i<tskSum;i++ ){
            if ( int(tskGrpId)==atoi(m_listCtrl.GetItemText(i,TSK_GRP)) && 
                int(tskId)==atoi(m_listCtrl.GetItemText(i,TSK_ID)) ){
                strPer.Format("%d %%",int( 100*LOWORD(tskMsg.lParam)*1.f/(HIWORD(tskMsg.lParam)) ) );
                m_listCtrl.SetItemText( i,TSK_STA,strPer  );
            }
        }
        break;
    case TSKPROC_OVER :
        for ( i=0;i<tskSum;i++ ){
            if ( int(tskGrpId)==atoi(m_listCtrl.GetItemText(i,TSK_GRP)) && int(tskId)==atoi(m_listCtrl.GetItemText(i,TSK_ID)) ){
                m_listCtrl.SetItemText( i,TSK_STA,"100 %%" );
            }
        }
        break;
    case TSKEXE_OVER  :
        for ( i=0;i<tskSum;i++ ){
            if ( int(tskGrpId)==atoi(m_listCtrl.GetItemText(i,TSK_GRP)) && int(tskId)==atoi(m_listCtrl.GetItemText(i,TSK_ID)) ){
                m_listCtrl.SetItemText( i,TSK_STA,TSK_STA_OVER );
            }
        }
        break;
    }   
}


double getKval( int kId,double sun_zenith,double view_zenith,double azimuth2sun ){
    double scatter = cos(sun_zenith) * cos(view_zenith) + sin(sun_zenith) * sin(view_zenith) * cos(azimuth2sun);
    double rTIM = 18.0 / PI;
    double br = 1.6; // BR for Li's models
    double hb = 2.3; // HB for Li's models
    double kxx = 0;
    switch (kId) {
    case 0:  kxx = 0.0;
        break;
    case 1:  RossThick(&kxx, sun_zenith, view_zenith, azimuth2sun);
        break;
    case 2:  RossThin(&kxx, sun_zenith, view_zenith, azimuth2sun);
        break;
    case 3:  Roujean(&kxx, sun_zenith, view_zenith, azimuth2sun);
        break;
    case 4:  LiSparse(&kxx, hb, br, sun_zenith, view_zenith, azimuth2sun);
        break;
    case 5:  LiDense(&kxx, hb, br, sun_zenith, view_zenith, azimuth2sun);
        break;
    case 6:  kxx = (view_zenith * rTIM) * cos(azimuth2sun);
        break;
    case 7:  kxx = sun_zenith * rTIM;
        break;
    case 8:  kxx = 1.0 / tan(sun_zenith);
        break;
        break;
    default: break;
    }// end of switch
    return kxx;
}

double getKval( int kId, double gx,double gy,double gz,
               double cx,double cy,double cz,double lon,double lat,
               int yy,int mm,int dd,int ho,int mi,int se,double *sz=NULL,double *vz=NULL,double *as=NULL )
{
    double		sun_zenith,view_zenith,azimuth2sun;
    getSunPos( gx,gy,gz,cx,cy,cz,lon,lat,yy,mm,dd,ho,mi,se,&sun_zenith,&view_zenith,&azimuth2sun );

    if ( sz ) *sz = sun_zenith;
    if ( vz ) *vz = view_zenith;
    if ( as ) *as = azimuth2sun;

    return getKval( kId,sun_zenith,view_zenith,azimuth2sun );
}

#include "WuErsImage.hpp"
#include "WuBmpFile.hpp"

inline WORD* GetBiPxlRS(float fx, float fy, int cols, int rows, int bnds,WORD *pBuf, WORD bkGrd = 0){    
    static WORD buf0[16]={bkGrd,bkGrd,bkGrd,bkGrd,bkGrd,bkGrd,bkGrd,bkGrd}; WORD *buf= buf0; 
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

static bool GetPxl(double gx,double gy,CWuErsImage *pImg,short *pv,double *fc0=NULL,double *fr0=NULL){

    int cols = pImg->GetCols();
    int rows = pImg->GetRows();
    int bnds = pImg->GetBands();
    int pxSz = pImg->PxByte( pImg->GetPxType() )*bnds;    
    double fc = (gx - pImg->m_tlX)/pImg->m_dx; 
    double fr = (rows-1) - (pImg->m_tlY - gy)/pImg->m_dy; 

    if(fc0)*fc0 = fc; if(fr0)*fr0 = (rows-1) - fr;

    WORD *pC = GetBiPxlRS( fc,fr,cols,rows,bnds,(WORD*)pImg->m_pImgDat,0 );
    pv[0]=pC[0]; pv[1]=pC[1]; pv[2]=pC[2]; pv[3]=pC[3];
    return true;
}

BOOL MchTie(LPCSTR lpstrPar)
{
    char strSrc[256],strRef[256],strDes[256],strTsk[256],str[512],strOlp[512];
    double s[4],a0[4],a1[4],a2[4],k1,k2,fc,fr,fc1,fr1,sz,vz,as,sz1,vz1,as1;
    double cx,cy,cz,phi,img,kap,grdZ,cx1,cy1,cz1,phi1,img1,kap1,grdZ1; 
    int yy,mm,dd,ho,mi,se,yy1,mm1,dd1,ho1,mi1,se1,utmZn=0,gs=21;
    const char *pS = strrchr(lpstrPar,'@'); if ( pS ){ pS++; while( *pS==' ' ) pS++; }else pS = lpstrPar; strcpy( strTsk,pS );
    FILE *fTsk = fopen( strTsk,"rt" );
    fgets( str,512,fTsk ); sscanf( str,"%s",strSrc ); DOS_PATH(strSrc);
    fgets( str,512,fTsk ); sscanf( str,"%lf%lf%lf%lf%lf%lf%lf%d%d%d%d%d%d%d%d", &cx, &cy, &cz,&phi,&img,&kap, &grdZ, &yy, &mm, &dd, &ho, &mi, &se,&utmZn,&gs );
    
    CWuErsImage domImg,basImg; strcat( strSrc,".ers" );
    if ( !domImg.Load4File(strSrc) ) return FALSE;
    int cols = domImg.GetCols();
    int rows = domImg.GetRows();
    int bnds = domImg.GetBands();
    int pxSz = domImg.PxByte( domImg.GetPxType() )*bnds;
    double gx,gy,gz,lon,lat,hei; short cv[4],rv[4],rvv[4]; 
    double avK1=0,avK2=0,ks=0; int r,c; CWuGeoCvt geoCvt; 
    geoCvt.Set_Cvt_Par( ET_WGS84,UTM_PROJECTION,SEMIMAJOR_WGS84,SEMIMINOR_WGS84,0,Zone2CenterMerdian(utmZn)*SPGC_D2R,500000,0,0.9996,0 );

    while( !feof(fTsk) ){
        if ( !fgets( str,512,fTsk ) ) break;
        sscanf( str,"%s", strRef ); 
        if ( !fgets( str,512,fTsk ) ) break;
        if ( sscanf(str, "%lf%lf%lf%lf%lf%lf%lf%d%d%d%d%d%d", &cx1, &cy1, &cz1,&phi1,&img1,&kap1, &grdZ1, &yy1, &mm1, &dd1, &ho1, &mi1, &se1 )<10 ) break;

        DOS_PATH(strRef); strcpy( strOlp,strTsk );  strcpy( strrchr(strOlp,'.'),"_" ); strcat( strOlp,strrchr(strRef,'\\')+1 ); strcat( strOlp,".olp" );  cprintf("%s\n",strOlp);      
        strcat( strRef,".ers" ); if ( !basImg.Load4File(strRef) ) continue;
        FILE *fOlp = fopen( strOlp,"wt" ); if ( !fOlp ) continue;
        for( r=1;r<rows-gs;r+=gs ){
            for(  c=1;c<cols-gs;c+=gs ){
                short *pC = (short*)(domImg.m_pImgDat+ (r*cols+c)* pxSz);
                if ( pC[0]==0 && pC[1]==0 && pC[2]==0 ) continue;

                gx = domImg.m_tlX + c*domImg.m_dx;
                gy = domImg.m_tlY - (rows-1-r)*domImg.m_dy;
                gz = grdZ;
                if ( !GetPxl(gx,gy,&domImg,cv,&fc ,&fr ) ) continue;
                if ( !GetPxl(gx,gy,&basImg,rv,&fc1,&fr1) ) continue;
                if ( rv[0]==0 && rv[1]==0 && rv[2]==0 ) continue;
                if ( rv[0]<0 || rv[1]<0 || rv[2]<0 || cv[0]<0 || cv[1]<0 || cv[2]<0 ) continue;

                geoCvt.Cvt_Prj2LBH( gx,gy,gz,&lon,&lat,&hei );
                getSunPos( gx,gy,gz,cx,cy,cz,lon*SPGC_R2D,lat*SPGC_R2D,yy,mm,dd,ho,mi,se,&sz,&vz,&as );                
                if ( yy1==0 ){ sz1 = vz1 = as1 = 0; } 
                else getSunPos( gx,gy,gz,cx1,cy1,cz1,lon*SPGC_R2D,lat*SPGC_R2D,yy1,mm1,dd1,ho1,mi1,se1,&sz1,&vz1,&as1 );

                fprintf( fOlp,"\t%.5f %.5f %.5f %6d %6d %6.1f %6.1f %6.1f %6.1lf %.5f %.5f %.5f %6d %6d %6.1f %6.1f %6.1f %6.1f\n",
                    sz,vz,as,int(fc),int(fr),cv[0]*1.0,cv[1]*1.0,cv[2]*1.0,cv[3]*1.0,
                    sz1,vz1,as1,int(fc1),int(fr1),rv[0]*1.0,rv[1]*1.0,rv[2]*1.0,rv[3]*1.0 );
            }
        }
        fclose(fOlp);
    }
    fclose(fTsk);
    return 0;
}


BOOL CalS2B0(LPCSTR lpstrPar)
{
    char strSrc[256],strBas[256],strDes[256],strTsk[256],str[256];
    double s[4],a0[4],a1[4],a2[4],k1,k2,cx,cy,cz,phi,img,kap,grdZ; int yy,mm,dd, ho, mi, se,utmZn=0,gs=21;
    const char *pS = strrchr(lpstrPar,'@'); if ( pS ){ pS++; while( *pS==' ' ) pS++; }else pS = lpstrPar; strcpy( strTsk,pS );
    FILE *fTsk = fopen( strTsk,"rt" );
    fscanf( fTsk,"%s",strSrc ); DOS_PATH(strSrc);
    fscanf(fTsk, "%lf%lf%lf%lf%lf%lf%lf%d%d%d%d%d%d%d%d", &cx, &cy, &cz,&phi,&img,&kap, &grdZ, &yy, &mm, &dd, &ho, &mi, &se,&utmZn,&gs );
    fscanf( fTsk,"%s", strBas); DOS_PATH(strBas);
    fclose(fTsk);
    
    strcat( strSrc,".ers" );
    strcat( strBas,".ers" );
    strcpy( strDes,strTsk ); strcpy( strrchr(strDes,'\\'),strrchr(strSrc,'\\') );
    
    CWuGeoCvt geoCvt; 
    geoCvt.Set_Cvt_Par( ET_WGS84,UTM_PROJECTION,SEMIMAJOR_WGS84,SEMIMINOR_WGS84,0,Zone2CenterMerdian(utmZn)*SPGC_D2R,500000,0,0.9996,0 );
    
    CWuErsImage domImg,basImg;
    if ( !domImg.Load4File(strSrc) ) return FALSE;
    if ( !basImg.Load4File(strBas) ) return FALSE;
    
    int cols = domImg.GetCols();
    int rows = domImg.GetRows();
    int bnds = domImg.GetBands();
    int pxSz = domImg.PxByte( domImg.GetPxType() )*bnds;
    
    #define PARANUM     4
    double aa1[PARANUM*PARANUM*4];  memset( aa1, 0,sizeof(double)*PARANUM*PARANUM*4 );
    double b1[PARANUM*4];           memset( b1 , 0,sizeof(double)*PARANUM*4 ); 
    double *aa2=aa1+PARANUM*PARANUM,*aa3=aa1+PARANUM*PARANUM*2,*aa4=aa1+PARANUM*PARANUM*3;
    double *b2=b1+PARANUM,*b3=b1+PARANUM*2,*b4=b1+PARANUM*3;
    double x[PARANUM];           memset( x , 0,sizeof(double)*PARANUM );        
    double a[PARANUM];           memset( a , 0,sizeof(double)*PARANUM );
    double res[PARANUM]={0,0,0,0},rZ=0;

    CWuErsImage di,bi,k1i,k2i;
    int dic = cols/gs,dir = rows/gs,ofi;
    di.CreateImg( dic,dir,4 );
    bi.CreateImg( dic,dir,4 );
    k1i.CreateImg( dic,dir,1 );
    k2i.CreateImg( dic,dir,1 );
    short *pdi = (short*)di.GetImgDat();
    short *pbi = (short*)bi.GetImgDat();
    short *pk1i = (short*)k1i.GetImgDat();
    short *pk2i = (short*)k2i.GetImgDat();
    memset( pdi,0,8*dic*dir );
    memset( pbi,0,8*dic*dir );
    memset( pk1i,0,2*dic*dir );
    memset( pk2i,0,2*dic*dir );

    s[0]=1.2890363562; a0[0]=11.2204856304; a1[0]=-3987.7716184755;  a2[0]=1266.7245665732;
    s[1]=1.3809901585; a0[1]=17.8360109217; a1[1]=-4884.1919314446;  a2[1]=1495.8332392236;
    s[2]=1.5071120933; a0[2]=19.0365112832; a1[2]=-3876.9519920289;  a2[2]=1164.3379559162;
    s[3]=0.8419036308; a0[3]=22.8582025890; a1[3]=-15129.9764009558; a2[3]=3556.4788302197;


    sprintf( str,"%s-olp.txt",strTsk ); double sz,vz,as,fc,fr;
    FILE *fT = fopen(str,"wt");
    
    double gx,gy,gz,lon,lat,hei; short cv[4],rv[4],rvv[4]; int r,c;
    double avK1=0,avK2=0,ks=0;
    for( r=0;r<rows;r+=gs ){
        for(  c=0;c<cols;c+=gs ){
            short *pC = (short*)(domImg.m_pImgDat+ (r*cols+c)* pxSz);
            if ( pC[0]==0 && pC[1]==0 && pC[2]==0 ) continue;
            
            gx = domImg.m_tlX + c*domImg.m_dx;
            gy = domImg.m_tlY - (rows-1-r)*domImg.m_dy;
            gz = grdZ;
            geoCvt.Cvt_Prj2LBH( gx,gy,gz,&lon,&lat,&hei ); 


            /*
            for( int t=0;t<24;t++ ){
                getSunPos( gx,gy,gz,cx,cy,cz,lon*SPGC_R2D,lat*SPGC_R2D,yy,mm,dd,ho+t,mi,se,&sz,&vz,&as );
                cprintf("t=%d sz= %lf as= %lf\n",t,sz,as );
            }*/
            
            getSunPos( gx,gy,gz,cx,cy,cz,lon*SPGC_R2D,lat*SPGC_R2D,yy,mm,dd,ho,mi,se,&sz,&vz,&as );
            k1 = getKval( 1,sz,vz,as );
            k2 = getKval( 4,sz,vz,as );

            //k1 = getKval( 1,gx,gy,gz,cx,cy,cz,lon*SPGC_R2D,lat*SPGC_R2D,yy,mm,dd,ho,mi,se,&sz,&vz,&as );
            //k2 = getKval( 4,gx,gy,gz,cx,cy,cz,lon*SPGC_R2D,lat*SPGC_R2D,yy,mm,dd,ho,mi,se,&sz,&vz,&as );
            avK1 += k1; avK2 += k2; ks += 1;
        }
    }
    avK1 /= ks; avK2 /= ks;
    cprintf("avK1 = %lf avK2 = %lf\n",avK1,avK2 );


    //FILE *fov = fopen("V:\\Dat_CM\\TestDmc\\HB2024_DMC\\Workflow\\ROM_S2B\\ROM_S2B\\=============lvl02-01002-col.tif_000000_013440.dom_S2B_L2A_20240823T101559_R10m_PS2024_DMC.olp","rt");
    FILE *fov = fopen("V:\\Dat_CM\\TestDmz\\JZ2023_DMZ\\Workflow\\PS2025_DMZ_DOM_1000mm_Sparse\\GF_MZ_20230924_13538_0377_rgb.tif-Dom_T49RFQ_20231024T030801_10m_DMZ.olp","rt");    
    double ft,fcv[4],frv[4],frvv[4],sz1,vz1,as1;
    int i,c1,r1,sum=0; fscanf( fov,"%d",&sum ); 
    for( i=0;i<sum;i++ ){
        fscanf( fov,"%lf%lf%lf%d%d%lf%lf%lf%lf%lf%lf%lf%d%d%lf%lf%lf%lf",
            &sz,&vz,&as,&c,&r,fcv,fcv+1,fcv+2,fcv+3,
            &ft,&ft,&ft,&c1,&r1,frv,frv+1,frv+2,frv+3 );

        gx = domImg.m_tlX + c*domImg.m_dx;
        gy = domImg.m_tlY - r*domImg.m_dy;
        gz = 450;
        geoCvt.Cvt_Prj2LBH( gx,gy,gz,&lon,&lat,&hei ); 
        getSunPos( gx,gy,gz,cx,cy,cz,lon*SPGC_R2D,lat*SPGC_R2D,yy,mm,dd,ho,mi,se,&sz1,&vz1,&as1 );
        


        k1 = getKval( 1,sz,vz,as );
        k2 = getKval( 4,sz,vz,as );
        k1 -= avK1; k2 -= avK2;

        a[0] = fcv[0]; a[1] = -1; a[2] = -k1; a[3] = -k2;            
        Nrml( a, PARANUM, frv[0], aa1, b1, 1 );  
        
        a[0] = fcv[1]; a[1] = -1; a[2] = -k1; a[3] = -k2;            
        Nrml( a, PARANUM, frv[1], aa2, b2, 1 );  
        
        a[0] = fcv[2]; a[1] = -1; a[2] = -k1; a[3] = -k2;            
        Nrml( a, PARANUM, frv[2], aa3, b3, 1 );  
        
        a[0] = fcv[3]; a[1] = -1; a[2] = -k1; a[3] = -k2;            
        Nrml( a, PARANUM, frv[3], aa4, b4, 1 ); 
        
        frvv[0] = s[0] * fcv[0] -( a0[0] + k1 * a1[0] + k2 * a2[0] );
        frvv[1] = s[1] * fcv[1] -( a0[1] + k1 * a1[1] + k2 * a2[1] );
        frvv[2] = s[2] * fcv[2] -( a0[2] + k1 * a1[2] + k2 * a2[2] );
        frvv[3] = s[3] * fcv[3] -( a0[3] + k1 * a1[3] + k2 * a2[3] );

        fprintf( fT,"%.4lf %.4lf %.4lf %4d %4d %5.1lf %5.1lf %5.1lf %5.1lf 0 0 0 %5d %5d %5.1lf %5.1lf %5.1lf %5.1lf \t %.3lf %.3lf  %.2lf %.2lf %.2lf %.2lf\n",
            sz,vz,as,c,r,fcv[0],fcv[1],fcv[2],fcv[3],c1,r1,frv[0],frv[1],frv[2],frv[3],
                k1,k2,frvv[0]-frv[0],frvv[1]-frv[1],frvv[2]-frv[2],frvv[3]-frv[3] );
    }
        
    /*    
    for( r=rows-1-200;r>=0;r-=gs ){
        for(  c=264;c<cols;c+=gs ){
            short *pC = (short*)(domImg.m_pImgDat+ (r*cols+c)* pxSz);
            if ( pC[0]==0 && pC[1]==0 && pC[2]==0 ) continue;

            gx = domImg.m_tlX + c*domImg.m_dx;
            gy = domImg.m_tlY - (rows-1-r)*domImg.m_dy;
            gz = grdZ;
            geoCvt.Cvt_Prj2LBH( gx,gy,gz,&lon,&lat,&hei ); 
            k1 = getKval( 1,gx,gy,gz,cx,cy,cz,lon*SPGC_R2D,lat*SPGC_R2D,yy,mm,dd,ho,mi,se,&sz,&vz,&as );
            k2 = getKval( 4,gx,gy,gz,cx,cy,cz,lon*SPGC_R2D,lat*SPGC_R2D,yy,mm,dd,ho,mi,se,&sz,&vz,&as );

            if ( !GetPxl(gx,gy,&domImg,cv) ) continue;
            if ( !GetPxl(gx,gy,&basImg,rv,&fc,&fr) ) continue;
            k1 -= avK1; k2 -= avK2;

            rvv[0] = s[0] * pC[0] -( a0[0] + k1 * a1[0] + k2 * a2[0] );
            rvv[1] = s[1] * pC[1] -( a0[1] + k1 * a1[1] + k2 * a2[1] );
            rvv[2] = s[2] * pC[2] -( a0[2] + k1 * a1[2] + k2 * a2[2] );
            rvv[3] = s[3] * pC[3] -( a0[3] + k1 * a1[3] + k2 * a2[3] );

            ofi = ( r/gs * dic + (c/gs) )*4;
            memcpy( pdi+ofi,cv,8 );
            memcpy( pbi+ofi,rv,8 );
            pk1i[ofi/4] = short( k1*1000 +10000);
            pk2i[ofi/4] = short( k2*1000 +10000);
            fprintf( fT,"%.4lf %.4lf %.4lf %4d %4d %5d %5d %5d %5d 0 0 0 %5d %5d %5d %5d %5d %5d \t %.3lf %.3lf  %d %d %d %d\n",
                sz,vz,as,c,rows-1-r,cv[0],cv[1],cv[2],cv[3],int(fc+0.5),int(fr+0.5),rv[0],rv[1],rv[2],rv[3],
                k1,k2,rvv[0]-rv[0],rvv[1]-rv[1],rvv[2]-rv[2],rvv[3]-rv[3] );
            
            //rv[0] = s[0] * cv[0] +( -a0[0] - k1 * a1[0] - k2 * a2[0] );
           
            a[0] = cv[0]; a[1] = -1; a[2] = -k1; a[3] = -k2;            
            Nrml( a, PARANUM, rv[0], aa1, b1, 1 );  

            a[0] = cv[1]; a[1] = -1; a[2] = -k1; a[3] = -k2;            
            Nrml( a, PARANUM, rv[1], aa2, b2, 1 );  

            a[0] = cv[2]; a[1] = -1; a[2] = -k1; a[3] = -k2;            
            Nrml( a, PARANUM, rv[2], aa3, b3, 1 );  

            a[0] = cv[3]; a[1] = -1; a[2] = -k1; a[3] = -k2;            
            Nrml( a, PARANUM, rv[3], aa4, b4, 1 );   
        }
    }
    di.CopyGeoInf(&domImg); di.m_dx *= gs; di.m_dy *= gs; 
    bi.CopyGeoInf(&domImg); bi.m_dx *= gs; bi.m_dy *= gs; 
    k1i.CopyGeoInf(&domImg);
    k2i.CopyGeoInf(&domImg);    
    sprintf( str,"%s-di.ers",strTsk ); di.Save2File( str );
    sprintf( str,"%s-bi.ers",strTsk ); bi.Save2File( str );
    sprintf( str,"%s-k1i.ers",strTsk ); k1i.Save2File( str );
    sprintf( str,"%s-k2i.ers",strTsk ); k2i.Save2File( str );
    */

    fclose(fT);

    memset( x , 0,sizeof(double)*PARANUM ); 
    Solve( aa1, b1, x, PARANUM, PARANUM ); s[0]=x[0]; a0[0]=-x[1]; a1[0]=-x[2]; a2[0]=-x[3];

    memset( x , 0,sizeof(double)*PARANUM ); 
    Solve( aa2, b2, x, PARANUM, PARANUM ); s[1]=x[0]; a0[1]=-x[1]; a1[1]=-x[2]; a2[1]=-x[3];
    
    memset( x , 0,sizeof(double)*PARANUM ); 
    Solve( aa2, b3, x, PARANUM, PARANUM ); s[2]=x[0]; a0[2]=-x[1]; a1[2]=-x[2]; a2[2]=-x[3];

    memset( x , 0,sizeof(double)*PARANUM ); 
    Solve( aa4, b4, x, PARANUM, PARANUM ); s[3]=x[0]; a0[3]=-x[1]; a1[3]=-x[2]; a2[3]=-x[3];


    rewind(fov);
    fscanf( fov,"%d",&sum ); 
    for( i=0;i<sum;i++ ){
        fscanf( fov,"%lf%lf%lf%d%d%lf%lf%lf%lf%lf%lf%lf%d%d%lf%lf%lf%lf",
            &sz,&vz,&as,&c,&r,fcv,fcv+1,fcv+2,fcv+3,
            &ft,&ft,&ft,&c1,&r1,frv,frv+1,frv+2,frv+3 );
        
        k1 = getKval( 1,sz,vz,as );
        k2 = getKval( 4,sz,vz,as );
        k1 -= avK1; k2 -= avK2;
        
        frvv[0] = s[0] * fcv[0] -( a0[0] + k1 * a1[0] + k2 * a2[0] );
        frvv[1] = s[1] * fcv[1] -( a0[1] + k1 * a1[1] + k2 * a2[1] );
        frvv[2] = s[2] * fcv[2] -( a0[2] + k1 * a1[2] + k2 * a2[2] );
        frvv[3] = s[3] * fcv[3] -( a0[3] + k1 * a1[3] + k2 * a2[3] );

        res[0] += fabs( rv[0] - frvv[0] );
        res[1] += fabs( rv[1] - frvv[1] );
        res[2] += fabs( rv[2] - frvv[2] );
        res[3] += fabs( rv[3] - frvv[3] );
        rZ ++;
    }
    fclose(fov);

    /*
    for(  r=gs;r<rows;r+=gs ){
        for(  c=gs;c<cols;c+=gs ){
            short *pC = (short*)(domImg.m_pImgDat+ (r*cols+c)* pxSz);
            if ( pC[0]==0 && pC[1]==0 && pC[2]==0 ) continue;
            
            gx = domImg.m_tlX + c*domImg.m_dx;
            gy = domImg.m_tlY - (rows-1-r)*domImg.m_dy;
            gz = grdZ;
            geoCvt.Cvt_Prj2LBH( gx,gy,gz,&lon,&lat,&hei ); 
            k1 = getKval( 1,gx,gy,gz,cx,cy,cz,lon*SPGC_R2D,lat*SPGC_R2D,yy,mm,dd,ho,mi,se );
            k2 = getKval( 4,gx,gy,gz,cx,cy,cz,lon*SPGC_R2D,lat*SPGC_R2D,yy,mm,dd,ho,mi,se );
            k1 -= avK1; k2 -= avK2;

            if ( !GetPxl(gx,gy,&domImg,cv) ) continue;
            if ( !GetPxl(gx,gy,&basImg,rv) ) continue;
            
            res[0] += fabs( rv[0] - ( s[0] * cv[0] -( a0[0] + k1 * a1[0] + k2 * a2[0] ) ) );
            res[1] += fabs( rv[1] - ( s[1] * cv[1] -( a0[1] + k1 * a1[1] + k2 * a2[1] ) ) );
            res[2] += fabs( rv[2] - ( s[2] * cv[2] -( a0[2] + k1 * a1[2] + k2 * a2[2] ) ) );
            res[3] += fabs( rv[3] - ( s[3] * cv[3] -( a0[3] + k1 * a1[3] + k2 * a2[3] ) ) );
            rZ ++;
        }
    }
    */


    res[0] = res[0]/rZ;       
    res[1] = res[1]/rZ;
    res[2] = res[2]/rZ;
    res[3] = res[3]/rZ;

    strcat( strTsk,".knl.txt" );
    FILE *f = fopen( strTsk,"wt" );
    if (f){
        fprintf( f,"%.9lf %.9lf %.9lf %.9lf \n",s[0],a0[0],a1[0],a2[0] );
        fprintf( f,"%.9lf %.9lf %.9lf %.9lf \n",s[1],a0[1],a1[1],a2[1] );
        fprintf( f,"%.9lf %.9lf %.9lf %.9lf \n",s[2],a0[2],a1[2],a2[2] );
        fprintf( f,"%.9lf %.9lf %.9lf %.9lf \n",s[3],a0[3],a1[3],a2[3] );

        fprintf( f,"\n===== res ===== sum= %.0lf === \n",rZ );
        fprintf( f,"%lf %lf %lf %lf\n",res[0],res[1],res[2],res[3] );
    }
    fclose(f);



    s[0]=1.2890363562; a0[0]=11.2204856304; a1[0]=-3987.7716184755;  a2[0]=1266.7245665732;
    s[1]=1.3809901585; a0[1]=17.8360109217; a1[1]=-4884.1919314446;  a2[1]=1495.8332392236;
    s[2]=1.5071120933; a0[2]=19.0365112832; a1[2]=-3876.9519920289;  a2[2]=1164.3379559162;
    s[3]=0.8419036308; a0[3]=22.8582025890; a1[3]=-15129.9764009558; a2[3]=3556.4788302197;

    CWuErsImage dv,wu;
    wu.Load4File("V:/Dat_CM/TestDmc/HB2024_DMC/Workflow/ROM_S2B/ROM_S2B--/lvl02-01002-col.tif_000000_013440.dom.ers");
    dv.CreateImg( cols,rows,4 );
    short *pV = (short*)dv.GetImgDat();
    memset( pV,0,8*cols*rows );
    
    BYTE *pV1 = new BYTE[cols*rows*4];
    BYTE *pV2 = pV1+cols*rows,*pV3=pV1+cols*rows*2,*pV4=pV1+cols*rows*3;
    memset( pV1,0,cols*rows*4 );

    for( r=0;r<rows;r++ ){
        for( c=0;c<cols;c++ ){
            short *pW = (short*)(wu.m_pImgDat+ (r*cols+c)* pxSz);
            short *pC = (short*)(domImg.m_pImgDat+ (r*cols+c)* pxSz);
            if ( pC[0]==0 && pC[1]==0 && pC[2]==0 ) continue;
            
            gx = domImg.m_tlX + c*domImg.m_dx;
            gy = domImg.m_tlY - (rows-1-r)*domImg.m_dy;
            gz = grdZ;
            geoCvt.Cvt_Prj2LBH( gx,gy,gz,&lon,&lat,&hei );             
            k1 = getKval( 1,gx,gy,gz,cx,cy,cz,lon*SPGC_R2D,lat*SPGC_R2D,yy,mm,dd,ho,mi,se );
            k2 = getKval( 4,gx,gy,gz,cx,cy,cz,lon*SPGC_R2D,lat*SPGC_R2D,yy,mm,dd,ho,mi,se );

            k1 -= avK1; k2 -= avK2;            
            rv[0] = s[0] * pC[0] -a0[0] - k1 * a1[0] - k2 * a2[0] ;
            rv[1] = s[1] * pC[1] -a0[1] - k1 * a1[1] - k2 * a2[1] ;
            rv[2] = s[2] * pC[2] -a0[2] - k1 * a1[2] - k2 * a2[2] ;
            rv[3] = s[3] * pC[3] -a0[3] - k1 * a1[3] - k2 * a2[3] ;
            
            pC[0] =short( rv[0] );  
            pC[1] =short( rv[1] );
            pC[2] =short( rv[2] );
            pC[3] =short( rv[3] );
            
            short *pD = (short*)(dv.m_pImgDat+ (r*cols+c)* pxSz);
            pD[0] =short( abs(pW[0]-rv[0]) );
            pD[1] =short( abs(pW[1]-rv[1]) );
            pD[2] =short( abs(pW[2]-rv[2]) );
            pD[3] =short( abs(pW[3]-rv[3]) );


            BYTE *pI = pV1+(r*cols+c);
            *pI = pD[0]>255?255:pD[0];
            
            pI += cols*rows;
            *pI = pD[1]>255?255:pD[1];

            pI += cols*rows;
            *pI = pD[2]>255?255:pD[1];

            pI += cols*rows;
            *pI = pD[3]>255?255:pD[1];
        }
    }
    domImg.Save2File(strDes);

    sprintf( str,"%s-dv.ers",strDes );
    dv.Save2File(str);


    sprintf( str,"%s-dv1.bmp",strDes ); Save2Bmp(str,pV1,cols,rows,1);
    sprintf( str,"%s-dv2.bmp",strDes ); Save2Bmp(str,pV2,cols,rows,1);
    sprintf( str,"%s-dv3.bmp",strDes ); Save2Bmp(str,pV3,cols,rows,1);
    sprintf( str,"%s-dv4.bmp",strDes ); Save2Bmp(str,pV4,cols,rows,1);

    delete[] pV1;
    return 0;
}
