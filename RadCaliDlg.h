// RadCaliDlg.h : header file
//
/*----------------------------------------------------------------------+
|		RadCaliDlg														|
|       Author:     DuanYanSong  2025/06/08 			                |
|            Ver 1.0													|
|       Copyright (c)2025, WHU RSGIS DPGrid Group                       |
|	         All rights reserved.                                       |
|		ysduan@whu.edu.cn; ysduan@sohu.com              				|
+----------------------------------------------------------------------*/

#if !defined(AFX_RADCALIDLG_H__40F95964_192C_4CF7_9B8B_758B5DC9A89D__INCLUDED_)
#define AFX_RADCALIDLG_H__40F95964_192C_4CF7_9B8B_758B5DC9A89D__INCLUDED_

#include "Resource.h"


#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000


#ifndef     WM_OUTPUT_MSG
#define		WM_OUTPUT_MSG		WM_USER + 2070
enum OUTMSG{
    PROC_MSG   =   10,
    PROC_START =   11,
    PROC_STEP  =   12,
    PROC_OVER  =   13,
    PROC_IDLE  =   20,
	PROC_FSCR  =   30,

    PROC_STARTSUB =   41,
    PROC_STEPSUB  =   42,
    PROC_OVERSUB  =   43,

	UPDATEDATA	  =   51,

    THREADEND	  =   61,
    TASKOVER      =   62,

};

#else
#pragma message("RadCaliDlg.h ,Warning: WM_OUTPUT_MSG alread define, be sure it was define as: WM_OUTPUT_MSG WM_USER+2070 ") 
#endif

/////////////////////////////////////////////////////////////////////////////
// CRadCaliDlg dialog
class CRadCaliDlg : public CDialog
{ 
// Construction
public:
	CRadCaliDlg(CWnd* pParent = NULL);	// standard constructor
    class CWuEdit: public CEdit
    {
    public:
        CWuEdit (){ };
        virtual ~CWuEdit(){ };
        LRESULT WindowProc( UINT message, WPARAM wParam, LPARAM lParam ){ 
            if ( message==WM_DROPFILES ){
                char fName[ FILENAME_MAX ]; 
                if ( ::DragQueryFile( HDROP(wParam), -1, NULL,0)>0 ){
                    ::DragQueryFile( HDROP(wParam),0,fName,FILENAME_MAX );	
                    SetWindowText( fName );
                }
            }
            return CEdit::WindowProc(message,wParam,lParam); 
        };
    };

    CString m_strDom;
    int     m_utmZn;

// Dialog Data
	//{{AFX_DATA(CRadCaliDlg)
	enum { IDD = IDD_RADCALI_DIALOG };
	CComboBox	m_cmbMxCore;
    CString	    m_strMxCore;
	CWuEdit	m_edtBas;
	CProgressCtrl	m_progCur;
	CProgressCtrl	m_progAll;
	CListCtrl	m_listCtrl;
    CWuEdit	    m_editRet;
    CString	    m_strRet;
	CString	m_strBas;
	int		m_gs;
	int		m_ws;
	BOOL	m_bTie;
	BOOL	m_bAdj;	
	BOOL	m_bTxt;
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CRadCaliDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support
    virtual BOOL OnInitDialog();
	//}}AFX_VIRTUAL

// Implementation
protected:
	HICON m_hIcon;
    // Generated message map functions
    afx_msg LRESULT OnOutputMsg( WPARAM wParam,LPARAM lParam );
	//{{AFX_MSG(CRadCaliDlg)	
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
    afx_msg void OnInitMenuPopup(CMenu* pPopupMenu, UINT nIndex, BOOL bSysMenu);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon(){ return (HCURSOR) m_hIcon; };
	afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnDropFiles(HDROP hDropInfo);
	afx_msg void OnDestroy();
	virtual void OnOK();
    afx_msg void OnButtonRet();
    afx_msg void OnKeydownListCtrl(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnButtonLoad();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

public:
    HANDLE	m_hThread;
    HANDLE  m_hEThEHdl,m_hW4EndHdl;
	void    Process();
public:
    void    OnTaskOver( UINT tskId );
    void    OnTaskTerm( UINT tskGrpId,UINT tskId );
    void    OnTaskExit( UINT tskGrpId,UINT tskId );

private:    
    void    PrintMsg(LPCSTR lpstrMsg ) {if ( ::IsWindow(m_hWnd) )::SendMessage( m_hWnd,WM_OUTPUT_MSG,PROC_MSG  ,LPARAM(lpstrMsg) );  }; 

    void    ProgBegin(int range)       {if ( ::IsWindow(m_hWnd) )::SendMessage( m_hWnd,WM_OUTPUT_MSG,PROC_START,range );           };
    void    ProgStep(int& cancel)      {if ( ::IsWindow(m_hWnd) )::SendMessage( m_hWnd,WM_OUTPUT_MSG,PROC_STEP ,LPARAM(&cancel) );   };
    void    ProgEnd()                  {if ( ::IsWindow(m_hWnd) )::SendMessage( m_hWnd,WM_OUTPUT_MSG,PROC_OVER ,0 );               };
    
    void    ProgBeginSub(int range)    {if ( ::IsWindow(m_hWnd) )::SendMessage( m_hWnd,WM_OUTPUT_MSG,PROC_STARTSUB,range );        };
    void    ProgStepSub(int& cancel)   {if ( ::IsWindow(m_hWnd) )::SendMessage( m_hWnd,WM_OUTPUT_MSG,PROC_STEPSUB ,LPARAM(&cancel) );};
    void    ProgEndSub()               {if ( ::IsWindow(m_hWnd) )::SendMessage( m_hWnd,WM_OUTPUT_MSG,PROC_OVERSUB ,0 );            };


};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_RADCALIDLG_H__40F95964_192C_4CF7_9B8B_758B5DC9A89D__INCLUDED_)

