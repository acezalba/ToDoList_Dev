// ToDoCtrl.cpp : implementation file
//

#include "stdafx.h"
#include "ToDoCtrl.h"
#include "taskfile.h"
#include "taskclipboard.h"
#include "tdcmsg.h"
#include "tdcmapping.h"
#include "tdstringres.h"
#include "resource.h"
#include "TDCtasktimelog.h"
#include "todoitem.h"
#include "tdcstatic.h"
#include "tdcenum.h"
#include "tdltaskicondlg.h"
#include "tdlreuserecurringtaskdlg.h"
#include "tdlimportoutlookobjectsdlg.h"
#include "tdccustomattributeUIhelper.h"
#include "tdladdloggedtimedlg.h"
#include "tdcoutlookimporthelper.h"
#include "ToDoCtrlDataDefines.h"
#include "TDCDialogHelper.h"

#include "..\shared\autoflag.h"
#include "..\shared\clipboard.h"
#include "..\shared\datehelper.h"
#include "..\shared\deferWndMove.h"
#include "..\shared\dlgunits.h"
#include "..\shared\driveinfo.h"
#include "..\shared\enbitmap.h"
#include "..\shared\enfiledialog.h"
#include "..\shared\enmenu.h"
#include "..\shared\enstring.h"
#include "..\shared\filemisc.h"
#include "..\shared\graphicsmisc.h"
#include "..\shared\holdredraw.h"
#include "..\shared\localizer.h"
#include "..\shared\mapex.h"
#include "..\shared\messagebox.h"
#include "..\shared\misc.h"
#include "..\shared\msoutlookhelper.h"
#include "..\shared\osversion.h"
#include "..\shared\passworddialog.h"
#include "..\shared\savefocus.h"
#include "..\shared\ScopedTimer.h"
#include "..\shared\stringres.h"
#include "..\shared\themed.h"
#include "..\shared\toolbarhelper.h"
#include "..\shared\treectrlhelper.h"
#include "..\shared\wclassdefines.h"
#include "..\shared\webmisc.h"
#include "..\shared\winclasses.h"

#include "..\3rdparty\msoutl.h"
#include "..\3rdparty\shellicons.h"
#include "..\3rdparty\colordef.h"
#include "..\3rdparty\dibdata.h"
#include "..\3rdparty\gdiplus.h"

#include "..\Interfaces\Preferences.h"
#include "..\interfaces\spellcheckdlg.h"
#include "..\interfaces\IContentControl.h"
#include "..\Interfaces\TasklistSchemaDef.h"

#include <Windowsx.h>
#include <float.h>
#include <Lmcons.h>
#include <math.h>
#include <afxpriv.h>
#include <shlwapi.h>

/////////////////////////////////////////////////////////////////////////////

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////

// In DLU
const int CTRLHEIGHT		= 13;
const int LABELHEIGHT		= 9; 
const int CTRLHSPACING		= 6; 
const int CTRLVSPACING		= 4; 
const int CTRLLEN			= 75;
const int COMMENTSTYPELEN	= 85;

/////////////////////////////////////////////////////////////////////////////

const int TIMETRACKPERIOD	= 10000; // 10 secs
const int MIDNIGHTPERIOD	= 60000; // 1 minute

/////////////////////////////////////////////////////////////////////////////

const int SPLITSIZE				= GraphicsMisc::ScaleByDPIFactor(6); 
const int DEFCOMMENTSIZE		= GraphicsMisc::ScaleByDPIFactor(260);
const int MINNONCOMMENTHEIGHT	= GraphicsMisc::ScaleByDPIFactor(250); // what's above the comment section
const int MINNONCOMMENTWIDTH	= GraphicsMisc::ScaleByDPIFactor(350); // what's to the left of the comment section
const int COMBODROPHEIGHT		= GraphicsMisc::ScaleByDPIFactor(200);
const int MINSTACKEDCOMMENTSIZE = GraphicsMisc::ScaleByDPIFactor(60);
const int INFOTIPOFFSET			= GraphicsMisc::ScaleByDPIFactor(16);

/////////////////////////////////////////////////////////////////////////////

const COLORREF BLACK	= RGB(0, 0, 0);
const COLORREF WHITE	= RGB(240, 240, 240);
const COLORREF MAGENTA	= RGB(255, 0, 255);

/////////////////////////////////////////////////////////////////////////////

const UINT  DAYINSECS		= 24 * 60 * 60;
const int	DECIMALS = 4;

/////////////////////////////////////////////////////////////////////////////

const DWORD IDD_TODOCTRL_DIALOG = (DWORD)(LPCTSTR)_T("IDD_TODOCTRL_DIALOG");

/////////////////////////////////////////////////////////////////////////////

const LPCTSTR ARCHIVE_ID = _T(".done");
const LPCTSTR DICTIONARY_URL = _T("https://github.com/abstractspoon/ToDoList_Downloads/wiki/Dictionaries");

//////////////////////////////////////////////////////////////////////////////

enum 
{
	ID_TIME_TRACK = 0xfff0,
	ID_ADD_TIME,
	ID_EXTERNALID_LINK,
	ID_DEPENDS_LINK,
};

enum // flags for UpdateTask
{
	UTF_TIMEUNITSONLY	= 0x01,
	UTF_RECALCTIME		= 0x02,		
};

//////////////////////////////////////////////////////////////////////////////
// private class to help optimize xml parsing

class CXmlParseController : public IXmlParse
{
public:
	CXmlParseController(LPCTSTR szItem) : m_sItem(szItem) {}
	
	virtual BOOL Continue(LPCTSTR szItem, LPCTSTR /*szValue*/) const
	{
		return (m_sItem.CompareNoCase(szItem) != 0);
	}
	
protected:
	CString m_sItem;
};

//////////////////////////////////////////////////////////////////////////////
// private macros

#define HOLD_REDRAW(tdc, tree) 	CHoldRedraw hr(tdc); CHoldRedraw hr2(tree);

//////////////////////////////////////////////////////////////////////////////
// private CToDoCtrl messages

UINT CToDoCtrl::WM_TDC_FIXUPPOSTDROPSELECTION		= (WM_APP + 1);
UINT CToDoCtrl::WM_TDC_REFRESHPERCENTSPINVISIBILITY	= (WM_APP + 2);
UINT CToDoCtrl::WM_TDC_RECREATERECURRINGTASK		= (WM_APP + 3);

//////////////////////////////////////////////////////////////////////////////

CToDoCtrl::CToDoCtrl(const CTDLContentMgr& mgr, const CONTENTFORMAT& cfDefault, const TDCCOLEDITFILTERVISIBILITY& visDefault) 
	: 
	m_bArchive(FALSE),
	m_bDelayLoaded(FALSE),
	m_bDeletingTasks(FALSE),
	m_bDragDropSubtasksAtTop(TRUE),
	m_bModified(FALSE), 
	m_bSplitting(FALSE),
	m_calculator(m_data),
	m_cbAllocBy(ACBS_ALLOWDELETE | ACBS_AUTOCOMPLETE),
	m_cbAllocTo(ACBS_ALLOWDELETE | ACBS_AUTOCOMPLETE),
	m_cbCategory(ACBS_ALLOWDELETE | ACBS_AUTOCOMPLETE),
	m_ctrlComments(TRUE, 85, &mgr),
	m_cbFileRef(FES_COMBOSTYLEBTN | FES_GOBUTTON | FES_ALLOWURL | FES_RELATIVEPATHS | FES_DISPLAYSIMAGES),
	m_cbStatus(ACBS_ALLOWDELETE | ACBS_AUTOCOMPLETE),
	m_cbTags(ACBS_ALLOWDELETE | ACBS_AUTOCOMPLETE),
	m_cbTimeDone(TCB_HALFHOURS | TCB_NOTIME | TCB_HOURSINDAY),
	m_cbTimeDue(TCB_HALFHOURS | TCB_NOTIME | TCB_HOURSINDAY),
	m_cbTimeStart(TCB_HALFHOURS | TCB_NOTIME | TCB_HOURSINDAY),
	m_cbVersion(ACBS_ALLOWDELETE | ACBS_AUTOCOMPLETE),
	m_cbPriority(FALSE),
	m_cbRisk(FALSE),
	m_cfDefault(cfDefault),
	m_dTrackedTimeElapsedHours(0),
	m_data(m_styles, m_aCustomAttribDefs),
	m_dwEditTitleTaskID(0),
	m_dwLastAddedID(0),
	m_dwNextUniqueID(1), 
	m_eTaskName(PEC_AUTODESTROY),
	m_hFontComments(NULL),
	m_hFontTree(NULL),
	m_mgrContent(mgr),
	m_nCommentsPos(TDCUIL_RIGHT),
	m_nCommentsSize(DEFCOMMENTSIZE),
	m_nCommentsState(CS_CLEAN),
	m_nControlsPos(TDCUIL_BOTTOM),
	m_nDefRecurFrom(TDIRO_DUEDATE),
	m_nDefRecurReuse(TDIRO_REUSE),
	m_nFileFormat(TDL_FILEFORMAT_CURRENT),
	m_nFileVersion(0),
	m_nMaxState(TDCMS_NORMAL),
	m_nMaxInfotipCommentsLength(-1),
	m_nPriority(-1),
	m_treeDragDrop(TSH(), m_taskTree.Tree()),
	m_visColEdit(visDefault),
	m_sXmlHeader(DEFAULT_UNICODE_HEADER),
	m_timeTracking(m_data, m_taskTree.TSH()),
	m_taskTree(m_ilTaskIcons, m_data, m_styles, m_tldAll, m_visColEdit.GetVisibleColumns(), m_aCustomAttribDefs),
	m_exporter(m_data, m_taskTree, m_mgrContent),
	m_formatter(m_data),
	m_infoTip(m_data, m_aCustomAttribDefs),
#pragma warning (disable: 4355)
	m_sourceControl(*this),
	m_findReplace(*this),
	m_reminders(*this),
	m_matcher(m_data, m_reminders)
#pragma warning (default: 4355)
{
	SetBordersDLU(0);
	
	// add controls
	for (int nCtrl = 0; nCtrl < NUM_TDCCTRLS; nCtrl++)
	{
		const TDCCONTROL& ctrl = TDCCONTROLS[nCtrl];

		AddRCControl(_T("CONTROL"), ctrl.szClass, CString((LPCTSTR)ctrl.nIDCaption), 
					ctrl.dwStyle, ctrl.dwExStyle,
					ctrl.nX, ctrl.nY, ctrl.nCx, ctrl.nCy, ctrl.nID);
	}
	
	// set up number masks
	m_ePercentDone.SetMask(_T("0123456789"));
	m_eCost.SetMask(_T("@-.0123456789"), ME_LOCALIZEDECIMAL);
	
	// add 'clock' button to 'time spent'
	m_iconTrackTime.Load(IDI_TIMETRACK);
	m_eTimeSpent.InsertButton(1, ID_TIME_TRACK, m_iconTrackTime, CEnString(IDS_TDC_STARTSTOPCLOCK), 15);

	// add 'Plus' button to 'time spent'
	m_iconAddTime.Load(IDI_ADD_LOGGED_TIME);
	m_eTimeSpent.InsertButton(2, ID_ADD_TIME, m_iconAddTime, CEnString(IDS_TDC_ADDLOGGEDTIME), 15);

	// add 'go to' button to dependency
	m_iconLink.Load(IDI_DEPENDS_LINK);
	m_eDependency.AddButton(ID_DEPENDS_LINK, m_iconLink, CEnString(IDS_TDC_DEPENDSLINK_TIP));

	// misc
	m_cpColour.SetSelectionMode(CP_MODE_TEXT);
	m_data.SetDefaultCommentsFormat(m_cfDefault);
}

CToDoCtrl::~CToDoCtrl()
{
}

void CToDoCtrl::DoDataExchange(CDataExchange* pDX)
{
	CRuntimeDlg::DoDataExchange(pDX);
	
	DDX_Control(pDX, IDC_ALLOCBY, m_cbAllocBy);
	DDX_Control(pDX, IDC_ALLOCTO, m_cbAllocTo);
	DDX_Control(pDX, IDC_CATEGORY, m_cbCategory);
	DDX_Control(pDX, IDC_COLOUR, m_cpColour);
	DDX_Control(pDX, IDC_COST, m_eCost);
	DDX_Control(pDX, IDC_DEPENDS, m_eDependency);
	DDX_Control(pDX, IDC_DONEDATE, m_dtcDone);
	DDX_Control(pDX, IDC_DONETIME, m_cbTimeDone);
	DDX_Control(pDX, IDC_DUEDATE, m_dtcDue);
	DDX_Control(pDX, IDC_DUETIME, m_cbTimeDue);
	DDX_Control(pDX, IDC_EXTERNALID, m_eExternalID);
	DDX_Control(pDX, IDC_FILEPATH, m_cbFileRef);
	DDX_Control(pDX, IDC_PERCENT, m_ePercentDone);
	DDX_Control(pDX, IDC_PERCENTSPIN, m_spinPercent);
	DDX_Control(pDX, IDC_PRIORITY, m_cbPriority);
	DDX_Control(pDX, IDC_RECURRENCE, m_eRecurrence);
	DDX_Control(pDX, IDC_RISK, m_cbRisk);
	DDX_Control(pDX, IDC_STARTDATE, m_dtcStart);
	DDX_Control(pDX, IDC_STARTTIME, m_cbTimeStart);
	DDX_Control(pDX, IDC_STATUS, m_cbStatus);
	DDX_Control(pDX, IDC_TAGS, m_cbTags);
	DDX_Control(pDX, IDC_TIMEEST, m_eTimeEstimate);
	DDX_Control(pDX, IDC_TIMESPENT, m_eTimeSpent);
	DDX_Control(pDX, IDC_VERSION, m_cbVersion);

	DDX_Text(pDX, IDC_DEPENDS, m_sDepends);
	DDX_Text(pDX, IDC_EXTERNALID, m_sExternalID);
	DDX_Text(pDX, IDC_PROJECTNAME, m_sProjectName);
	DDX_ColourPicker(pDX, IDC_COLOUR, m_crColour);

	CTDCDialogHelper::DDX_Text(pDX, IDC_COST, m_cost, DECIMALS);
	CTDCDialogHelper::DDX_Text(pDX, IDC_PERCENT, m_nPercentDone, m_spinPercent);

	CTDCDialogHelper::DDX_Text(pDX, m_eTimeEstimate, m_timeEstimate, DECIMALS);
	CTDCDialogHelper::DDX_Text(pDX, m_eTimeSpent, m_timeSpent, DECIMALS);

	m_cbAllocBy.DDX(pDX, m_sAllocBy);
	m_cbStatus.DDX(pDX, m_sStatus);
	m_cbVersion.DDX(pDX, m_sVersion);
	m_cbPriority.DDX(pDX, m_nPriority);
	m_cbRisk.DDX(pDX, m_nRisk);
	m_eRecurrence.DDX(pDX, m_tRecurrence);
	m_cbFileRef.DDX(pDX, m_aFileRefs);
	
	CTDCCustomAttributeUIHelper::DDX(pDX, m_aCustomControls, m_aCustomAttribDefs, m_mapCustomCtrlData);

	// custom
	if (pDX->m_bSaveAndValidate)
	{
		if (m_crColour == CLR_DEFAULT)
			m_crColour = CLR_NONE; // unset
	}
	else
	{
	}
}

void CToDoCtrl::UpdateComments(BOOL bSaveAndValidate)
{
	if (bSaveAndValidate)
	{
		m_ctrlComments.GetContent(m_sTextComments, m_customComments);
	}
	else
	{
		int nSelCount = GetSelectedCount();

		if (m_sTextComments.IsEmpty() && (nSelCount > 1))
		{
			m_ctrlComments.ClearContent();
		}
		else
		{
			BOOL bCommentsFocused = m_ctrlComments.HasFocus();
			m_ctrlComments.SetContent(m_sTextComments, m_customComments, !bCommentsFocused);
		}

		CEnString sComboPrompt((nSelCount > 1) ? IDS_TDC_EDITPROMPT_MULTIPLEFORMATS : IDS_TDC_EDITPROMPT_UNKNOWNFORMAT);
		CEnString sCommentsPrompt((nSelCount > 1) ? IDS_TDC_EDITPROMPT_MULTIPLETASKS : IDS_TDC_EDITPROMPT_COMMENTS);

		m_ctrlComments.SetWindowPrompts(sComboPrompt, sCommentsPrompt);
	}
}

BEGIN_MESSAGE_MAP(CToDoCtrl, CRuntimeDlg)
//{{AFX_MSG_MAP(CToDoCtrl)
	//}}AFX_MSG_MAP
	ON_WM_SETCURSOR()
	ON_WM_CTLCOLOR()
	ON_WM_SIZE()
	ON_WM_ERASEBKGND()
	ON_WM_DESTROY()
	ON_WM_CONTEXTMENU()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_SETCURSOR()
	ON_WM_CAPTURECHANGED()
	ON_WM_SHOWWINDOW()
	ON_WM_TIMER()
	ON_WM_SETTINGCHANGE()
	ON_WM_HELPINFO()

	ON_NOTIFY(NM_CLICK, IDC_TASKTREELIST, OnTreeClick)
	ON_NOTIFY(TVN_SELCHANGED, IDC_TASKTREELIST, OnTreeSelChange)

	ON_REGISTERED_MESSAGE(WM_ICC_CONTENTCHANGE, OnCommentsChange)
	ON_REGISTERED_MESSAGE(WM_ICC_KILLFOCUS, OnCommentsKillFocus)
	ON_REGISTERED_MESSAGE(WM_ICC_WANTSPELLCHECK, OnCommentsWantSpellCheck)
	ON_REGISTERED_MESSAGE(WM_ICC_DOHELP, OnCommentsDoHelp)
	ON_REGISTERED_MESSAGE(WM_ICC_HASCLIPBOARD, OnTDCHasClipboard)
	ON_REGISTERED_MESSAGE(WM_ICC_GETCLIPBOARD, OnTDCGetClipboard)
	ON_REGISTERED_MESSAGE(WM_ICC_TASKLINK, OnTDCDoTaskLink)
	ON_REGISTERED_MESSAGE(WM_ICC_FAILEDLINK, OnTDCFailedLink)
	ON_REGISTERED_MESSAGE(WM_ICC_GETLINKTOOLTIP, OnCommentsGetTooltip)

	ON_REGISTERED_MESSAGE(WM_TDCN_COLUMNEDITCLICK, OnTDCColumnEditClick)
	ON_REGISTERED_MESSAGE(WM_TDCM_GETTASKREMINDER, OnTDCGetTaskReminder)
	ON_REGISTERED_MESSAGE(WM_TDCM_FAILEDLINK, OnTDCFailedLink)
	ON_REGISTERED_MESSAGE(WM_TDCM_ISTASKDONE, OnTDCTaskIsDone)

	ON_CBN_EDITCHANGE(IDC_DONETIME, OnSelChangeDoneTime)
	ON_CBN_EDITCHANGE(IDC_DUETIME, OnSelChangeDueTime)
	ON_CBN_EDITCHANGE(IDC_STARTTIME, OnSelChangeStartTime)
	ON_CBN_SELCHANGE(IDC_ALLOCBY, OnSelChangeAllocBy)
	ON_CBN_SELCHANGE(IDC_ALLOCTO, OnSelChangeAllocTo)
	ON_CBN_SELCHANGE(IDC_CATEGORY, OnSelChangeCategory)
	ON_CBN_SELCHANGE(IDC_DONETIME, OnSelChangeDoneTime)
	ON_CBN_SELCHANGE(IDC_DUETIME, OnSelChangeDueTime)
	ON_CBN_SELCHANGE(IDC_FILEPATH, OnSelChangeFileRefPath)
	ON_CBN_SELCHANGE(IDC_PRIORITY, OnChangePriority)
	ON_CBN_SELCHANGE(IDC_RISK, OnChangeRisk)
	ON_CBN_SELCHANGE(IDC_STARTTIME, OnSelChangeStartTime)
	ON_CBN_SELCHANGE(IDC_STATUS, OnSelChangeStatus)
	ON_CBN_SELCHANGE(IDC_TAGS, OnSelChangeTag)
	ON_CBN_SELCHANGE(IDC_VERSION, OnSelChangeVersion)
	ON_CBN_SELENDCANCEL(IDC_FILEPATH, OnCancelChangeFileRefPath)
	ON_CBN_SELENDCANCEL(IDC_ALLOCTO, OnSelCancelAllocTo)
	ON_CBN_SELENDCANCEL(IDC_CATEGORY, OnSelCancelCategory)
	ON_CBN_SELENDCANCEL(IDC_TAGS, OnSelCancelTag)
	ON_CBN_SELENDCANCEL(IDC_ALLOCBY, OnSelCancelAllocBy)
	ON_CBN_SELENDCANCEL(IDC_PRIORITY, OnSelCancelPriority)
	ON_CBN_SELENDCANCEL(IDC_RISK, OnSelCancelRisk)
	ON_CBN_SELENDCANCEL(IDC_STATUS, OnSelCancelStatus)
	ON_CBN_SELENDCANCEL(IDC_VERSION, OnSelCancelVersion)
	ON_CBN_SELENDOK(IDC_COMMENTS, OnSelChangeCommentsType)
	ON_EN_CHANGE(IDC_COST, OnChangeCost)
	ON_EN_CHANGE(IDC_DEPENDS, OnChangeDependency)
	ON_EN_CHANGE(IDC_EXTERNALID, OnChangeExternalID)
	ON_EN_CHANGE(IDC_PERCENT, OnChangePercent)
	ON_EN_CHANGE(IDC_PROJECTNAME, OnChangeProjectName)
	ON_EN_CHANGE(IDC_RECURRENCE, OnChangeRecurrence)
	ON_EN_CHANGE(IDC_TIMEEST, OnChangeTimeEstimate)
	ON_EN_CHANGE(IDC_TIMESPENT, OnChangeTimeSpent)
	ON_MESSAGE(CPN_SELENDOK, OnChangeColour)
	ON_MESSAGE(WM_GETFONT, OnGetFont)
	ON_MESSAGE(WM_TDC_RECREATERECURRINGTASK, OnRecreateRecurringTask)
	ON_MESSAGE(WM_TDC_REFRESHPERCENTSPINVISIBILITY, OnRefreshPercentSpinVisibility)
	ON_MESSAGE(WM_TDC_FIXUPPOSTDROPSELECTION, OnFixupPostDropSelection)
	ON_NOTIFY(DTN_DATETIMECHANGE, IDC_DONEDATE, OnCompletionDatechange)
	ON_NOTIFY(DTN_DATETIMECHANGE, IDC_DUEDATE, OnDueDatechange)
	ON_NOTIFY(DTN_DATETIMECHANGE, IDC_STARTDATE, OnStartDatechange)
	ON_REGISTERED_MESSAGE(WM_ACBN_ITEMADDED, OnAutoComboAddDelete)
	ON_REGISTERED_MESSAGE(WM_ACBN_ITEMDELETED, OnAutoComboAddDelete)
	ON_REGISTERED_MESSAGE(WM_DD_DRAGABORT, OnTreeDragAbort)
	ON_REGISTERED_MESSAGE(WM_DD_DRAGDROP, OnTreeDragDrop)
	ON_REGISTERED_MESSAGE(WM_DD_DRAGENTER, OnTreeDragEnter)
	ON_REGISTERED_MESSAGE(WM_DD_DRAGOVER, OnTreeDragOver)
	ON_REGISTERED_MESSAGE(WM_DD_PREDRAGMOVE, OnTreeDragPreMove)
	ON_REGISTERED_MESSAGE(WM_FE_DISPLAYFILE, OnFileEditDisplayFile)
	ON_REGISTERED_MESSAGE(WM_FE_GETFILEICON, OnFileEditWantIcon)
	ON_REGISTERED_MESSAGE(WM_PCANCELEDIT, OnLabelEditCancel)
	ON_REGISTERED_MESSAGE(WM_PENDEDIT, OnLabelEditEnd)
	ON_REGISTERED_MESSAGE(WM_TDL_APPLYADDLOGGEDTIME, OnApplyAddLoggedTime)
	ON_REGISTERED_MESSAGE(WM_TEN_UNITSCHANGE, OnTimeUnitsChange)
	ON_REGISTERED_MESSAGE(WM_TLDT_DROP, OnDropObject)
	ON_REGISTERED_MESSAGE(WM_TLDT_CANDROP, OnCanDropObject)
 	ON_REGISTERED_MESSAGE(WM_EE_BTNCLICK, OnEEBtnClick)
	ON_REGISTERED_MESSAGE(WM_FINDREPLACE, OnFindReplaceMsg)
	ON_REGISTERED_MESSAGE(WM_TDCFR_GETEXCLUSIONRECT, OnFindReplaceGetExclusionRect)
	ON_REGISTERED_MESSAGE(WM_TDCFR_REPLACESELTASK, OnFindReplaceSelectedTask)
	ON_REGISTERED_MESSAGE(WM_TDCFR_REPLACEALLTASKS, OnFindReplaceAllTasks)
	ON_REGISTERED_MESSAGE(WM_TDCFR_SELECTNEXTTASK, OnFindReplaceSelectNextTask)
	ON_REGISTERED_MESSAGE(WM_TDCTI_RELOADICONS, OnTaskIconDlgReloadIcons)

	ON_NOTIFY_RANGE(DTN_DATETIMECHANGE, IDC_FIRST_CUSTOMEDITFIELD, IDC_LAST_CUSTOMEDITFIELD, OnCustomAttributeChange)
	ON_CONTROL_RANGE(EN_CHANGE, IDC_FIRST_CUSTOMEDITFIELD, IDC_LAST_CUSTOMEDITFIELD, OnCustomAttributeChange)
	ON_CONTROL_RANGE(CBN_SELCHANGE, IDC_FIRST_CUSTOMEDITFIELD, IDC_LAST_CUSTOMEDITFIELD, OnCustomAttributeChange)
	ON_CONTROL_RANGE(CBN_SELENDCANCEL, IDC_FIRST_CUSTOMEDITFIELD, IDC_LAST_CUSTOMEDITFIELD, OnCustomAttributeCancel)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CToDoCtrl message handlers

BOOL CToDoCtrl::ParseTaskLink(const CString& sLink, BOOL bURL, DWORD& dwTaskID, CString& sFile) const
{
	return ParseTaskLink(sLink, bURL, FileMisc::GetFolderFromFilePath(m_sLastSavePath), dwTaskID, sFile);
}

BOOL CToDoCtrl::ParseTaskLink(const CString& sLink, BOOL bURL, const CString& sFolder, DWORD& dwTaskID, CString& sFile)
{
	return TODOITEM::ParseTaskLink(sLink, bURL, sFolder, dwTaskID, sFile);
}

CString CToDoCtrl::FormatTaskLink(DWORD dwTaskID, BOOL bFull) const
{
	if (!dwTaskID || (bFull && !HasFilePath()))
	{
		ASSERT(0);
		return _T("");
	}

	return TODOITEM::FormatTaskLink(dwTaskID, TRUE, (bFull ? m_sLastSavePath : _T("")));
}

CString CToDoCtrl::FormatTaskDependency(DWORD dwTaskID, BOOL bFull) const
{
	if (!dwTaskID || (bFull && !HasFilePath()))
	{
		ASSERT(0);
		return _T("");
	}

	return TODOITEM::FormatTaskLink(dwTaskID, FALSE, (bFull ? m_sLastSavePath : _T("")));
}

BOOL CToDoCtrl::IsReservedShortcut(DWORD dwShortcut)
{
	return CTDLTaskCtrlBase::IsReservedShortcut(dwShortcut);
}

void CToDoCtrl::EnableExtendedSelection(BOOL bCtrl, BOOL bShift)
{
	CTDLTaskCtrlBase::EnableExtendedSelection(bCtrl, bShift);
}

void CToDoCtrl::SetRecentlyModifiedPeriod(const COleDateTimeSpan& dtSpan)
{
	TODOITEM::SetRecentlyModifiedPeriod(dtSpan);
}

TDC_FILEFMT CToDoCtrl::CompareFileFormat() const
{
    if (m_nFileFormat < TDL_FILEFORMAT_CURRENT)
	{
		return TDCFF_OLDER;
	}
    else if (m_nFileFormat > TDL_FILEFORMAT_CURRENT)
	{
        return TDCFF_NEWER;
	}
    
	// else
	return TDCFF_SAME;
}

void CToDoCtrl::EnableEncryption(BOOL bEnable)
{
	if (!bEnable && !m_sPassword.IsEmpty())
	{
		m_sPassword.Empty();
		SetModified(TDCA_ENCRYPT);
	}
	else if (bEnable && m_sPassword.IsEmpty())
	{
		CEnString sExplanation(IDS_ENABLEENCRYPTION);

		if (CPasswordDialog::RetrievePassword(TRUE, m_sPassword, sExplanation))
			SetModified(TDCA_ENCRYPT);
	}
}

BOOL CToDoCtrl::WantPasswordReprompting() const
{
	return !HasStyle(TDCS_DISABLEPASSWORDPROMPTING);
}

BOOL CToDoCtrl::VerifyPassword(const CString& sExplanation) const
{
	if (!IsEncrypted())
		return TRUE; // no password to check

	return CPasswordDialog::VerifyPassword(m_sPassword, sExplanation);
}

BOOL CToDoCtrl::CanEncrypt()
{
	return CTaskFile::CanEncrypt();
}

BOOL CToDoCtrl::Create(const CRect& rect, CWnd* pParentWnd, UINT nID, BOOL bVisible, BOOL bEnabled)
{
	DWORD dwStyle = (WS_CHILD | (bVisible ? WS_VISIBLE : 0) | (bEnabled ? 0 : WS_DISABLED) | WS_TABSTOP);
	CLocalizer::IgnoreString(_T("ToDoCtrl"));
	
	return CRuntimeDlg::Create(_T("ToDoCtrl"), dwStyle | DS_SETFONT, WS_EX_CONTROLPARENT, rect, pParentWnd, nID);
}

BOOL CToDoCtrl::OnInitDialog() 
{
	// create the tree-list before anything else
	CRect rCtrl;
	GraphicsMisc::GetAvailableScreenSpace(*this, rCtrl);

	VERIFY(m_taskTree.Create(this, rCtrl, IDC_TASKTREELIST));

	// create rest of controls
	CRuntimeDlg::OnInitDialog();
	
	// comments
	VERIFY(m_ctrlComments.Create(this, IDC_COMMENTS));
	
	// disable translation of auto-combos
	CLocalizer::EnableTranslation(m_cbAllocBy, FALSE);
	CLocalizer::EnableTranslation(m_cbAllocTo, FALSE);
	CLocalizer::EnableTranslation(m_cbCategory, FALSE);
	CLocalizer::EnableTranslation(m_cbStatus, FALSE);
	CLocalizer::EnableTranslation(m_cbVersion, FALSE);
	CLocalizer::EnableTranslation(m_cbTags, FALSE);

	// percent spin
	m_spinPercent.SetRange(0, 100);
	m_spinPercent.SetBuddy(GetDlgItem(IDC_PERCENT));
	
	UDACCEL uda = { 0, m_nPercentIncrement };
	m_spinPercent.SetAccel(1, &uda);
	
	// init dates
	m_dtcStart.SendMessage(DTM_SETSYSTEMTIME, GDT_NONE, 0);
	m_dtcDue.SendMessage(DTM_SETSYSTEMTIME, GDT_NONE, 0);
	m_dtcDone.SendMessage(DTM_SETSYSTEMTIME, GDT_NONE, 0);
	
	m_dtTree.Register(&m_taskTree.Tree(), this);
	m_dtFileRef.Register(&m_cbFileRef, this); 

	// task icon image list
	LoadTaskIcons();
	
	// custom font
	if (m_hFontTree)
		m_taskTree.SetFont(m_hFontTree);

	InitEditPrompts();

	m_cpColour.SetWindowText(CEnString(IDS_COLOR_SAMPLETEXT));
	m_cpColour.SetDefaultText(CEnString(IDS_COLOR_AUTOMATIC));
	m_cpColour.SetCustomText(CEnString(IDS_COLOR_MORECOLORS));
	
	// tree drag drop
	m_treeDragDrop.Initialize(this);
	
	// enabled states
	UpdateControls();
	SetFocusToTasks();

	// notify parent that we have been created
	CWnd* pParent = GetParent();

	if (pParent)
		pParent->SendMessage(WM_PARENTNOTIFY, MAKEWPARAM(WM_CREATE, GetDlgCtrlID()), (LPARAM)GetSafeHwnd());

	// Start the timer which checks for midnight (day changeover)
	// which runs persistently
	SetTimer(TIMER_MIDNIGHT, MIDNIGHTPERIOD, NULL);
	
	return FALSE;  // return TRUE unless you set the focus to a control
	// EXCEPTION: OCX Property Pages should return FALSE
}

void CToDoCtrl::LoadTaskIcons()
{
	VERIFY(m_ilTaskIcons.LoadImages(m_sLastSavePath, MAGENTA, HasStyle(TDCS_SHOWDEFAULTTASKICONS)));

	OnTaskIconsChanged();
}

void CToDoCtrl::InitEditPrompts()
{
	m_mgrPrompts.SetEditPrompt(IDC_PROJECTNAME, *this, IDS_TDC_EDITPROMPT_PROJECT);
	m_mgrPrompts.SetEditPrompt(m_eExternalID, IDS_TDC_EDITPROMPT_EXTID);
	m_mgrPrompts.SetEditPrompt(m_eDependency, IDS_TDC_EDITPROMPT_DEPENDS); 

	m_mgrPrompts.SetComboPrompt(m_cbFileRef, IDS_TDC_EDITPROMPT_FILEREF);
	m_mgrPrompts.SetComboPrompt(m_cbAllocBy, IDS_TDC_EDITPROMPT_NAME);
	m_mgrPrompts.SetComboPrompt(m_cbAllocTo, IDS_TDC_EDITPROMPT_NAME);
	m_mgrPrompts.SetComboPrompt(m_cbCategory, IDS_TDC_EDITPROMPT_CATEGORY);
	m_mgrPrompts.SetComboPrompt(m_cbTags, IDS_TDC_EDITPROMPT_TAGS);
	m_mgrPrompts.SetComboPrompt(m_cbStatus, IDS_TDC_EDITPROMPT_STATUS);
	m_mgrPrompts.SetComboPrompt(m_cbVersion, IDS_TDC_EDITPROMPT_VER);
	
	m_mgrPrompts.SetComboPrompt(m_cbTimeDue.GetSafeHwnd(), CTimeHelper::FormatClockTime(23, 59));
	m_mgrPrompts.SetComboPrompt(m_cbTimeStart.GetSafeHwnd(), CTimeHelper::FormatClockTime(0, 0));

	// tree handles their own
	m_taskTree.SetWindowPrompt(CEnString(IDS_TDC_TASKLISTPROMPT));

	// Comments prompts set in UpdateComments()
}

BOOL CToDoCtrl::SetTreeFont(HFONT hFont)
{
	ASSERT(hFont);

	if (hFont && !GraphicsMisc::SameFontNameSize(hFont, m_hFontTree))
	{
		m_hFontTree = hFont;

		if (m_infoTip.GetSafeHwnd())
			m_infoTip.SetFont(CFont::FromHandle(hFont));

		if (m_taskTree.GetSafeHwnd())
			return m_taskTree.SetFont(hFont);
	}

	// no change
	return FALSE;
}

BOOL CToDoCtrl::SetCommentsFont(HFONT hFont)
{
	ASSERT(hFont);

	if (hFont && !GraphicsMisc::SameFontNameSize(hFont, m_hFontComments))
	{
		m_hFontComments = hFont;

#ifdef _DEBUG
		CString sFaceName;
		int nPointSize = GraphicsMisc::GetFontNameAndPointSize(m_hFontComments, sFaceName);

		ASSERT(!sFaceName.IsEmpty());
		ASSERT(nPointSize > 0);
#endif

		m_ctrlComments.SetContentFont(m_hFontComments);

		// we've had some trouble with plugins using the richedit control 
		// so after a font change we always resend the content
		m_ctrlComments.SetContent(m_sTextComments, m_customComments, FALSE);

		return TRUE;
	}

	// no change
	return FALSE;
}

void CToDoCtrl::ResizeAttributeColumnsToFit()
{
	m_taskTree.RecalcAllColumnWidths();
}

void CToDoCtrl::SetMaximizeState(TDC_MAXSTATE nState)
{
	HandleUnsavedComments();

	if (m_nMaxState != nState)
	{
		m_nMaxState = nState;

		if (GetSafeHwnd())
		{
			Invalidate(FALSE);
			Resize();
			UpdateControls(FALSE); // don't update comments
			
			// make sure focus is set correctly
			switch (nState)
			{
			case TDCMS_NORMAL:
				m_taskTree.EnsureSelectionVisible();
				break;
				
			case TDCMS_MAXTASKLIST:
				if (!HasStyle(TDCS_SHOWCOMMENTSALWAYS) || !m_ctrlComments.HasFocus())
				{
					SetFocusToTasks();
				}
				break;
				
			case TDCMS_MAXCOMMENTS:
				m_ctrlComments.SetFocus();
				break;
			}
		}
	}
}

void CToDoCtrl::OnSize(UINT nType, int cx, int cy) 
{
	CRuntimeDlg::OnSize(nType, cx, cy);
	
	EndLabelEdit(TRUE);
	Resize(cx, cy);
}

void CToDoCtrl::Resize(int cx, int cy, BOOL bSplitting)
{
	if (m_taskTree.GetSafeHwnd())
	{
		if (!cx && !cy)
		{
			// Ignore resizes until we receive the first proper one
			if (!HasInitialSize())
			{
				CRect rClient;
				GetClientRect(rClient);

				cx = rClient.right;
				cy = rClient.bottom;
			}
		}
		else
		{
			ClearInitialSize();
		}

		ValidateCommentsSize();
		ShowHideControls();

		CRect rAvailable(0, 0, cx, cy);
		{
			CDeferWndMove dwm(100);

			if (GetStackCommentsAndControls())
			{
				ReposControls(&dwm, rAvailable, bSplitting);
				ReposComments(&dwm, rAvailable);
			}
			else
			{
				ReposComments(&dwm, rAvailable);
				ReposControls(&dwm, rAvailable, bSplitting);
			}

			ReposProjectName(&dwm, rAvailable);
			ReposTaskTree(&dwm, rAvailable);
		}

		UpdateWindow();
		UpdateSelectedTaskPath();
	}
}

int CToDoCtrl::GetDefaultControlHeight() const
{
	// To handle DPI scaling better simply use the height of the category combo
	return GetChildHeight(&m_cbCategory);
}

void CToDoCtrl::ReposProjectName(CDeferWndMove* pDWM, CRect& rAvailable)
{
	// project name
	CRect rLabel = GetCtrlRect(IDC_PROJECTLABEL); 
	CRect rProject = GetCtrlRect(IDC_PROJECTNAME); 

	int nOffset = (rAvailable.left - rLabel.left);
	int nHeight = GetDefaultControlHeight();

	rProject.left += nOffset;
	rProject.right = rAvailable.right;
	rProject.bottom = rProject.top + nHeight;
	
	rLabel.OffsetRect(nOffset, 0);
	rLabel.top = rProject.top;
	rLabel.bottom = rProject.bottom;

	pDWM->MoveWindow(GetDlgItem(IDC_PROJECTLABEL), rLabel);
	pDWM->MoveWindow(GetDlgItem(IDC_PROJECTNAME), rProject);

	if (m_nMaxState != TDCMS_MAXTASKLIST && HasStyle(TDCS_SHOWPROJECTNAME))
		rAvailable.top = rProject.bottom + CDlgUnits(this).ToPixelsY(2);
	else
		rAvailable.top = rProject.top;
}

BOOL CToDoCtrl::CalcRequiredControlsRect(const CRect& rAvailable, CRect& rRequired, 
										 int& nCols, int& nRows, BOOL bPreserveSplitPos) const
{
	rRequired.SetRectEmpty();
	nCols = nRows = 0;

	if (m_nMaxState == TDCMS_MAXTASKLIST) // nothing to do
		return FALSE;

	// first count up the visible controls
	// so we can allocate the correct amount of space
	CTDCControlArray aControls;
	int nVisibleCtrls = GetControls(aControls, TRUE);
	
	if (nVisibleCtrls == 0) // nothing to do
		return FALSE;
	
	// figure out number of ctrl rows / columns
	nCols = nRows = 2;
	BOOL bStackedWithComments = GetStackCommentsAndControls();
	BOOL bStackCommentsAbove = HasStyle(TDCS_STACKCOMMENTSABOVEEDITS);
	
	CDlgUnits dlu(this);

	const int nCtrlHeight = GetDefaultControlHeight();
	const int nLabelHeight = dlu.ToPixelsY(LABELHEIGHT);
	const int nVSpacing = dlu.ToPixelsY(CTRLVSPACING);
	const int nCtrlWidth = dlu.ToPixelsX(CTRLLEN);
	const int nHSpacing = dlu.ToPixelsX(CTRLHSPACING);

	if (HasStyle(TDCS_AUTOREPOSCTRLS))
	{
		int nAvailHeight = -1, nAvailWidth = -1;

		switch (m_nControlsPos)
		{
		case TDCUIL_RIGHT: // vertical
		case TDCUIL_LEFT: // vertical
			if (bStackedWithComments && bPreserveSplitPos)
				nAvailWidth = m_nCommentsSize;
			else
				nAvailHeight = rAvailable.Height();
			break;
			
		case TDCUIL_BOTTOM: // horizontal
			if (bStackedWithComments && bPreserveSplitPos)
				nAvailHeight = m_nCommentsSize;
			else
				nAvailWidth = rAvailable.Width();
			break;
		}

		if (nAvailHeight > 0)
		{
			// Account of the 'extra' CTRLVSPACING that will occur
			// after the last column we add it into our calculations
			nAvailHeight += nVSpacing;

			nRows = max(2, nAvailHeight / (nCtrlHeight + nLabelHeight + nVSpacing));
			nCols = (nVisibleCtrls / nRows) + ((nVisibleCtrls % nRows) ? 1 : 0);

			// recalc actual rows used
			nRows = (nVisibleCtrls / nCols) + ((nVisibleCtrls % nCols) ? 1 : 0);
		}
		else if (nAvailWidth > 0)
		{
			// Account of the 'extra' CTRLHSPACING that will occur
			// after the last column we add it into our calculations
			nAvailWidth += nHSpacing;

			nCols = max(2, nAvailWidth / (nCtrlWidth + nHSpacing));
			nRows = (nVisibleCtrls / nCols) + ((nVisibleCtrls % nCols) ? 1 : 0);
		}
	}
	else // fixed controls
	{
		switch (m_nControlsPos)
		{
		case TDCUIL_RIGHT: // vertical
		case TDCUIL_LEFT:
			{
				nRows = 12;
				nCols = (nVisibleCtrls / nRows) + ((nVisibleCtrls % nRows) ? 1 : 0);

				// recalc actual rows used
				nRows = (nVisibleCtrls / nCols) + ((nVisibleCtrls % nCols) ? 1 : 0);
			}
			break;
			
		case TDCUIL_BOTTOM: // horizontal
			{
				nCols = 2;
				nRows = (nVisibleCtrls / nCols) + ((nVisibleCtrls % nCols) ? 1 : 0);
			}
			break;
		}
	}
	
	// remembering to take account of the 'taking-account' 
	// of the 'extra' spacing above
	rRequired = rAvailable;
	
	int nRequiredWidth = ((nCols * (nCtrlWidth + nHSpacing)) - nHSpacing);
	int nRequiredHeight = ((nRows * (nCtrlHeight + nLabelHeight + nVSpacing)) - nVSpacing);

	switch (m_nControlsPos)
	{
	case TDCUIL_RIGHT: // vertical
		if (bStackedWithComments)
		{
			if (bStackCommentsAbove)
				rRequired.top = (rRequired.bottom - nRequiredHeight);
			else
				rRequired.bottom = (rRequired.top + nRequiredHeight);

			if (bPreserveSplitPos)
				nRequiredWidth = m_nCommentsSize;
		}
		else
		{
			rRequired.top += 2;
		}
		rRequired.left = (rRequired.right - nRequiredWidth);
		break;
		
	case TDCUIL_LEFT:
		if (bStackedWithComments)
		{
			if (bStackCommentsAbove)
				rRequired.top = (rRequired.bottom - nRequiredHeight);
			else
				rRequired.bottom = (rRequired.top + nRequiredHeight);

			if (bPreserveSplitPos)
				nRequiredWidth = m_nCommentsSize;
		}
		else
		{
			rRequired.top += 2;
		}
		rRequired.right = (rRequired.left + nRequiredWidth);
		break;
		
	case TDCUIL_BOTTOM: // horizontal
		if (bStackedWithComments)
		{
			if (bStackCommentsAbove)
				rRequired.left = (rRequired.right - nRequiredWidth);
			else
				rRequired.right = (rRequired.left + nRequiredWidth);

			// align controls at top
			rRequired.top = (rAvailable.bottom - m_nCommentsSize);
			rRequired.bottom = (rRequired.top + nRequiredHeight);
		}
		else
		{
			rRequired.top = (rRequired.bottom - nRequiredHeight);
		}
		break;
	}

	return TRUE;
}

void CToDoCtrl::ReposControls(CDeferWndMove* pDWM, CRect& rAvailable, BOOL bSplitting)
{
	// only required when controls are visible
	if (m_nMaxState != TDCMS_NORMAL)
		return;
	
	CRect rCtrls;
	int nCols, nRows;

	// First we try to preserve the current split size
	if (!CalcRequiredControlsRect(rAvailable, rCtrls, nCols, nRows, TRUE))
		return;
	
	// If stacking and not splitting, we then check whether 
	// that leaves enough space for the comments, and if not 
	// we resize again allowing the splitter pos to move
	BOOL bStackCommentsAndControls = GetStackCommentsAndControls();
	BOOL bCtrlsFit = TRUE;

	if (!bSplitting && bStackCommentsAndControls)
	{
		BOOL bStackCommentsAbove = HasStyle(TDCS_STACKCOMMENTSABOVEEDITS);
		CRect rStackedAvail(rAvailable);

		switch (m_nControlsPos)
		{
		case TDCUIL_RIGHT: // vertical
		case TDCUIL_LEFT:
			if (bStackCommentsAbove)
			{
				bCtrlsFit = ((rCtrls.top - rAvailable.top) >= MINSTACKEDCOMMENTSIZE);

				if (!bCtrlsFit)
					rStackedAvail.top += MINSTACKEDCOMMENTSIZE;
			}
			else
			{
				bCtrlsFit = ((rAvailable.bottom - rCtrls.bottom) >= MINSTACKEDCOMMENTSIZE);

				if (!bCtrlsFit)
					rStackedAvail.bottom -= MINSTACKEDCOMMENTSIZE;
			}
			break;
			
		case TDCUIL_BOTTOM: // horizontal
			if (bStackCommentsAbove)
			{
				bCtrlsFit = ((rCtrls.left - rAvailable.left) >= MINSTACKEDCOMMENTSIZE);

				if (!bCtrlsFit)
					rStackedAvail.left += MINSTACKEDCOMMENTSIZE;
			}
			else
			{
				bCtrlsFit = ((rAvailable.right - rCtrls.right) >= MINSTACKEDCOMMENTSIZE);

				if (!bCtrlsFit)
					rStackedAvail.right -= MINSTACKEDCOMMENTSIZE;
			}
			break;
		}

		if (!bCtrlsFit)
		{
			// Allow split pos to move
			if (!CalcRequiredControlsRect(rStackedAvail, rCtrls, nCols, nRows, FALSE))
				return;
		}
	}

	if (!bCtrlsFit)
	{
		// adjust splitter pos
		int nNewCommentsSize = m_nCommentsSize;

		switch (m_nControlsPos)
		{
		case TDCUIL_RIGHT: // vertical
			nNewCommentsSize = (rAvailable.right - rCtrls.left);
			break;
			
		case TDCUIL_LEFT:
			nNewCommentsSize = rCtrls.right;
			break;
			
		case TDCUIL_BOTTOM: // horizontal
			nNewCommentsSize = rCtrls.top;
			break;
		}

		if (nNewCommentsSize > 0)
			m_nCommentsSize = nNewCommentsSize;
	}
	else if (!bStackCommentsAndControls) // adjust available area
	{
		switch (m_nControlsPos)
		{
		case TDCUIL_RIGHT: // vertical
			rAvailable.right = rCtrls.left - SPLITSIZE;
			break;
			
		case TDCUIL_LEFT:
			rAvailable.left = rCtrls.right + SPLITSIZE;
			break;
			
		case TDCUIL_BOTTOM: // horizontal
			rAvailable.bottom = rCtrls.top - SPLITSIZE;
			break;
		}
	}

	// now iterate the visible controls settings their positions dynamically
	CTDCControlArray aControls;
	int nVisibleCtrls = GetControls(aControls, TRUE);
	
	ASSERT(nVisibleCtrls);
	
	CDlgUnits dlu(this);

	const int nCtrlHeight = GetDefaultControlHeight();
	const int nLabelHeight = dlu.ToPixelsY(LABELHEIGHT);
	const int nVSpacing = dlu.ToPixelsY(CTRLVSPACING);
	const int nCtrlWidth = dlu.ToPixelsX(CTRLLEN);
	const int nHSpacing = dlu.ToPixelsX(CTRLHSPACING);

	int nXPos = 0, nYPos = 0;
	int nWidth = rCtrls.Width();

	for (int nCtrl = 0; nCtrl < aControls.GetSize(); nCtrl++)
	{
		const CTRLITEM& ctrl = aControls[nCtrl];
		ASSERT(IsCtrlShowing(ctrl));
		
		if ((nCtrl != 0) && ((nCtrl % nCols) == 0))
		{
			// move to next line
			nXPos = 0;
			nYPos += (nCtrlHeight + nLabelHeight + nVSpacing);
		}
		
		// Move label
		CRect rCtrl(nXPos, nYPos, nXPos + nCtrlWidth, nYPos + nLabelHeight);
		rCtrl.OffsetRect(rCtrls.TopLeft());

		pDWM->MoveWindow(GetDlgItem(ctrl.nLabelID), rCtrl);
		
		// move ctrl
		rCtrl.OffsetRect(0, nLabelHeight);
		rCtrl.bottom = (rCtrl.top + nCtrlHeight);

		ReposControl(ctrl, pDWM, rCtrl, rCtrls.right);
		
		// update XPos for the control
		nXPos += (nCtrlWidth + nHSpacing);
	}
}

int CToDoCtrl::GetControls(CTDCControlArray& aControls, BOOL bVisible) const
{
	aControls.RemoveAll();

	// standard controls, except for file link which is always last
	int nCtrl;
	for (nCtrl = 0; nCtrl < NUM_CTRLITEMS - 1; nCtrl++)
	{
		CTRLITEM ctrl = CTRLITEMS[nCtrl];

		if (!bVisible || IsCtrlShowing(ctrl))
			aControls.Add(ctrl);
	}
	
	// custom attribs
	for (nCtrl = 0; nCtrl < m_aCustomControls.GetSize(); nCtrl++)
	{
		CUSTOMATTRIBCTRLITEM ctrl = m_aCustomControls[nCtrl];
		aControls.Add(ctrl);

		// Buddy Control
		CTRLITEM buddy;

		if (ctrl.GetBuddy(buddy) && ctrl.IsShowingBuddy())
			aControls.Add(buddy);
	}

	// finally file link
	CTRLITEM ctrlFileRef = CTRLITEMS[NUM_CTRLITEMS - 1];

	if (IsCtrlShowing(ctrlFileRef))
		aControls.Add(ctrlFileRef);

	return aControls.GetSize();
}

void CToDoCtrl::ReposControl(const CTRLITEM& ctrl, CDeferWndMove* pDWM,
							 const CRect& rItem, int nClientRight)
{
	CRect rCtrl(rItem);

	// some special cases
	switch (ctrl.nCtrlID)
	{
	case IDC_PERCENT:
		{
			CRect rSpin = GetCtrlRect(IDC_PERCENTSPIN); // gets current pos
			rSpin.OffsetRect(rCtrl.right - rSpin.right, 0);
			rSpin.top = rCtrl.top;
			rSpin.bottom = rCtrl.bottom;
			pDWM->MoveWindow(&m_spinPercent, rSpin);
			
			rCtrl.right = rSpin.left;
		}
		break;
		
	case IDC_ALLOCTO:
	case IDC_ALLOCBY:
	case IDC_STATUS:
	case IDC_CATEGORY:
	case IDC_TAGS:
	case IDC_PRIORITY:
	case IDC_VERSION:
	case IDC_RISK:
	case IDC_FILEPATH:
		{
			// file path control can take as much space as is left
			if (ctrl.nCtrlID == IDC_FILEPATH)
				rCtrl.right = nClientRight;

			// don't move it if it hasn't actually moved
			// to prevent flickering. Note we do this 
			// before we add 200 to the bottom of the rect
			CRect rPos;
			GetCtrlRect(ctrl.nCtrlID);

			if (rPos == rCtrl)
				return; 

			// else
			rCtrl.bottom += COMBODROPHEIGHT;
		}
		break;

	// handle custom attributes
	default:
		if (CTDCCustomAttributeUIHelper::IsCustomEditControl(ctrl.nCtrlID))
		{
			TDCCUSTOMATTRIBUTEDEFINITION attribDef;

			if (m_aCustomAttribDefs.GetAttributeDef(ctrl.nAttrib, attribDef))
			{
				if (attribDef.IsList())
				{
					// same as combos above
					CRect rPos;
					GetCtrlRect(ctrl.nCtrlID);
					
					if (rPos == rCtrl)
						return; 
					
					// else
					rCtrl.bottom += COMBODROPHEIGHT;
				}
			}
		}
		break;
	}

	pDWM->MoveWindow(GetDlgItem(ctrl.nCtrlID), rCtrl);
}

void CToDoCtrl::ReposTaskTree(CDeferWndMove* pDWM, const CRect& rAvailable)
{
	pDWM->MoveWindow(&m_taskTree, rAvailable);
}

BOOL CToDoCtrl::IsSplitterVisible() const
{
	return ((m_nMaxState == TDCMS_NORMAL) || 
			((m_nMaxState == TDCMS_MAXTASKLIST) && HasStyle(TDCS_SHOWCOMMENTSALWAYS)));
}

BOOL CToDoCtrl::IsCommentsVisible(BOOL bActually) const
{
	if (m_nMaxState == TDCMS_MAXCOMMENTS)
		return TRUE; // always

	// comments are visible if splitter is
	BOOL bVisible = IsSplitterVisible();

	// check optionally for actual size
	if (bActually)
		bVisible &= (m_nCommentsSize > 0);

	return bVisible;
}

void CToDoCtrl::ReposComments(CDeferWndMove* pDWM, CRect& rAvailable /*in/out*/) 
{
	CRect rComments(rAvailable);

	BOOL bMaxTasklist = (m_nMaxState == TDCMS_MAXTASKLIST);
	BOOL bMaxComments = (m_nMaxState == TDCMS_MAXCOMMENTS);

	if (bMaxComments)
	{
		rAvailable.SetRectEmpty();
	}
	else if (bMaxTasklist && !HasStyle(TDCS_SHOWCOMMENTSALWAYS))
	{
		rComments.SetRectEmpty();
	}
	else
	{
		BOOL bStackCommentsAndControls = (GetStackCommentsAndControls() && !GetVisibleEditFields().IsEmpty());
	
		if (bStackCommentsAndControls)
		{
			CRect rCtrls;
			int nCols, nRows;
			
			if (CalcRequiredControlsRect(rAvailable, rCtrls, nCols, nRows, TRUE))
			{
				BOOL bStackCommentsAbove = HasStyle(TDCS_STACKCOMMENTSABOVEEDITS);

				switch (m_nCommentsPos)
				{
				case TDCUIL_RIGHT: // vertical
					{
						if (bStackCommentsAbove)
							rComments.bottom = rCtrls.top - SPLITSIZE;
						else
							rComments.top = rCtrls.bottom + SPLITSIZE;

						rComments.left = rAvailable.right - m_nCommentsSize;
						rComments.right = rCtrls.right;

						rAvailable.right = rComments.left - SPLITSIZE;
					}
					break;

				case TDCUIL_LEFT: // vertical
					{
						if (bStackCommentsAbove)
							rComments.bottom = rCtrls.top - SPLITSIZE;
						else
							rComments.top = rCtrls.bottom + SPLITSIZE;

						rComments.left = rAvailable.left;
						rComments.right = rAvailable.left + m_nCommentsSize;

						rAvailable.left = rComments.right + SPLITSIZE;
					}
					break;
					
				case TDCUIL_BOTTOM: // horizontal
					{
						if (bStackCommentsAbove)
							rComments.right = rCtrls.left - SPLITSIZE;
						else
							rComments.left = rCtrls.right + SPLITSIZE;

						rComments.top = rCtrls.top;
						rComments.bottom = rCtrls.top + m_nCommentsSize;

						const int PADDING = (SPLITSIZE / 2);
						rAvailable.bottom = rComments.top - (SPLITSIZE + PADDING);
					}
					break;
				}
			}
		}
		else
		{
			switch (m_nCommentsPos)
			{
			case TDCUIL_RIGHT: // vertical
				{
					rComments.left = rAvailable.right - m_nCommentsSize;

					rAvailable.right = rComments.left - SPLITSIZE;
				}
				break;
				
			case TDCUIL_LEFT:
				{
					rComments.right = rAvailable.left + m_nCommentsSize;

					rAvailable.left = rComments.right + SPLITSIZE;
				}
				break;
				
			case TDCUIL_BOTTOM: // horizontal
				{
					rComments.left = rAvailable.left;
					rComments.top = rAvailable.bottom - m_nCommentsSize;

					const int PADDING = (SPLITSIZE / 2);
					rAvailable.bottom = rComments.top - (SPLITSIZE + PADDING);
				}
				break;
			}
		}
	}

	if (!rComments.IsRectEmpty())
		pDWM->MoveWindow(GetDlgItem(IDC_COMMENTS), rComments);
}

void CToDoCtrl::ShowHideControl(const CTRLITEM& ctrl)
{
	CWnd* pCtrl = GetDlgItem(ctrl.nCtrlID);
	CWnd* pLabel = GetDlgItem(ctrl.nLabelID);
	
	if (!pCtrl || !pLabel)
		return;
	
	BOOL bCtrlShowing = IsCtrlShowing(ctrl);
	int nShowCtrl = bCtrlShowing ? SW_SHOW : SW_HIDE;
	
	pCtrl->ShowWindow(nShowCtrl);
	pLabel->ShowWindow(nShowCtrl);
	
	// some additions and modifications
	switch (ctrl.nCtrlID)
	{
	case IDC_PERCENT:
		m_spinPercent.ShowWindow(nShowCtrl);
		break;
	}
}

void CToDoCtrl::ShowHideControls()
{
	// now show/hide appropriate controls
	int nCtrl;
	for (nCtrl = 0; nCtrl < NUM_CTRLITEMS; nCtrl++)
	{
		const CTRLITEM& ctrl = CTRLITEMS[nCtrl];
		ShowHideControl(ctrl);
	}

	// always show custom controls
	for (nCtrl = 0; nCtrl < m_aCustomControls.GetSize(); nCtrl++)
	{
		const CUSTOMATTRIBCTRLITEM& ctrl = m_aCustomControls[nCtrl];
		ShowHideControl(ctrl);

		// Handle buddy control
		CTRLITEM buddy;

		if (ctrl.GetBuddy(buddy) && ctrl.IsShowingBuddy())
			ShowHideControl(buddy);
	}

	// Comments as required
	BOOL bCommentsVis = IsCommentsVisible(TRUE);
	m_ctrlComments.ShowWindow(bCommentsVis ? SW_SHOW : SW_HIDE);

	// task tree
	UpdateTasklistVisibility();
	
	// project name
	BOOL bMaximize = (m_nMaxState != TDCMS_NORMAL);
	BOOL bShowProjectName = !bMaximize && HasStyle(TDCS_SHOWPROJECTNAME);
	ShowCtrls(IDC_PROJECTLABEL, IDC_PROJECTNAME, bShowProjectName);
}

void CToDoCtrl::EnableDisableCustomControl(const CUSTOMATTRIBCTRLITEM& ctrl, DWORD dwTaskID, BOOL bEnable, BOOL bReadOnly)
{
	// Main control first
	EnableDisableControl(ctrl, dwTaskID, bEnable, bReadOnly, FALSE);

	// Buddy
	CTRLITEM buddy;

	if (ctrl.GetBuddy(buddy))
	{
		TDCCUSTOMATTRIBUTEDEFINITION attribDef;
		m_aCustomAttribDefs.GetAttributeDef(ctrl.sAttribID, attribDef);

		switch (attribDef.GetDataType())
		{
		case TDCCA_STRING:
		case TDCCA_INTEGER:	
		case TDCCA_DOUBLE:	
		case TDCCA_BOOL:
		case TDCCA_ICON:
			break;
			
		case TDCCA_DATE:
			// Buddy is time
			if (bEnable && !bReadOnly)
			{
				TDCCADATA data;
				GetSelectedTaskCustomAttributeData(attribDef.sUniqueID, data, FALSE);
				
				if (!CDateHelper::IsDateSet(data.AsDate()))
				{
					bReadOnly = TRUE;

					// clear any existing value
					CTimeComboBox* pBuddy = (CTimeComboBox*)GetDlgItem(ctrl.nBuddyCtrlID);
					ASSERT(pBuddy);

					if (pBuddy)
						pBuddy->SetOleTime(-1);
				}
			}
		}
		
		EnableDisableControl(buddy, dwTaskID, bEnable, bReadOnly, FALSE);
	}
}

void CToDoCtrl::EnableDisableControl(const CTRLITEM& ctrl, DWORD dwTaskID, BOOL bEnable, BOOL bReadOnly, BOOL bIsParent)
{
	CWnd* pCtrl = GetDlgItem(ctrl.nCtrlID);
	CWnd* pLabel = GetDlgItem(ctrl.nLabelID);
	
	if (!pCtrl || !pLabel)
		return;
	
	BOOL bCtrlShowing = IsCtrlShowing(ctrl);
	
	// control state
	RT_CTRLSTATE nCtrlState = (!bEnable || !bCtrlShowing) ? RTCS_DISABLED : 
								(bReadOnly ? RTCS_READONLY : RTCS_ENABLED);
	RT_CTRLSTATE nLabelState = (CThemed::IsAppThemed() && bCtrlShowing) ? RTCS_ENABLED : nCtrlState;
	
	// some additions and modifications
	switch (ctrl.nCtrlID)
	{
	case IDC_FILEPATH: // special case
		if (!bEnable)
		{
			m_cbFileRef.SetReadOnly(FALSE);
			m_cbFileRef.EnableWindow(FALSE);
		}
		else
		{
			m_cbFileRef.EnableWindow(TRUE);
			m_cbFileRef.SetReadOnly(bReadOnly);
		}
		return;

	case IDC_PERCENT:
		{
			BOOL bEditPercent = !HasStyle(TDCS_AUTOCALCPERCENTDONE);

			// restrictions on parents
			if (bEditPercent && bIsParent)
			{
				bEditPercent = (!HasStyle(TDCS_AVERAGEPERCENTSUBCOMPLETION) && 
								HasStyle(TDCS_ALLOWPARENTTIMETRACKING));
			}

			if (!bEditPercent && bEnable)
				nCtrlState = RTCS_READONLY;
			
			SetCtrlState(m_spinPercent, nCtrlState);
		}
		break;
		
	case IDC_TIMEEST:
		if (bEnable)
		{
			BOOL bEditTime = (!bIsParent || HasStyle(TDCS_ALLOWPARENTTIMETRACKING));

			if (!bEditTime)
				nCtrlState = RTCS_READONLY;
		}
		break;
		
	case IDC_TIMESPENT:
		if (bEnable)
		{
			BOOL bEditTime = (!bIsParent || HasStyle(TDCS_ALLOWPARENTTIMETRACKING));

			if (!bEditTime || (dwTaskID && m_timeTracking.IsTrackingTask(dwTaskID)))
				nCtrlState = RTCS_READONLY;
		}
		break;
		
	case IDC_DUETIME:
		if ((nCtrlState == RTCS_ENABLED) && !SelectedTaskHasDate(TDCD_DUE))
			nCtrlState = RTCS_READONLY;
		break;
		
	case IDC_STARTDATE:
		if ((nCtrlState == RTCS_ENABLED) && !CanEditSelectedTask(TDCA_STARTDATE))
			nCtrlState = RTCS_READONLY;
		break;
		
	case IDC_STARTTIME:
		if ((nCtrlState == RTCS_ENABLED) && !CanEditSelectedTask(TDCA_STARTTIME))
			nCtrlState = RTCS_READONLY;
		break;

	case IDC_DONETIME:
		if ((nCtrlState == RTCS_ENABLED) && !SelectedTaskHasDate(TDCD_DONE))
			nCtrlState = RTCS_READONLY;
		break;
	}
	
	SetCtrlState(*pLabel, nLabelState);
	SetCtrlState(*pCtrl, nCtrlState);
}

void CToDoCtrl::EnableDisableControls(HTREEITEM hti)
{
	DWORD dwTaskID = GetTaskID(hti);
	
	BOOL bMaximized = (m_nMaxState != TDCMS_NORMAL);
	BOOL bEnable = (hti && !bMaximized);
	BOOL bIsParent = TSH().ItemsAreAllParents();
	BOOL bReadOnly = IsReadOnly();
	BOOL bReadOnlyCtrls = (bReadOnly || m_taskTree.SelectionHasLocked(FALSE));

	// now enable/disable appropriate controls
	int nCtrl;
	for (nCtrl = 0; nCtrl < NUM_CTRLITEMS; nCtrl++)
	{
		const CTRLITEM& ctrl = CTRLITEMS[nCtrl];
		EnableDisableControl(ctrl, dwTaskID, bEnable, bReadOnlyCtrls, bIsParent);
	}

	// and custom controls
	for (nCtrl = 0; nCtrl < m_aCustomControls.GetSize(); nCtrl++)
	{
		const CUSTOMATTRIBCTRLITEM& ctrl = m_aCustomControls[nCtrl];
		EnableDisableCustomControl(ctrl, dwTaskID, bEnable, bReadOnlyCtrls);
	}

	// comments
	CONTENTFORMAT cfComments;
	GetSelectedTaskCustomComments(cfComments);
	BOOL bEditComments = (m_mgrContent.FindContent(cfComments) != -1);
	
	BOOL bCommentsVis = IsCommentsVisible();
	RT_CTRLSTATE nCommentsState = RTCS_ENABLED, nComboState = RTCS_ENABLED;
	
	if (!bCommentsVis || !hti)
	{
		nComboState = nCommentsState = RTCS_DISABLED;
	}
	else if (bReadOnlyCtrls)
	{
		nComboState = nCommentsState = RTCS_READONLY;
	}
	else if (!bEditComments)
	{
		nCommentsState = RTCS_READONLY;
	}

	m_ctrlComments.SetCtrlStates(nComboState, nCommentsState);

	// project name
	BOOL bShowProjectName = (!bMaximized && HasStyle(TDCS_SHOWPROJECTNAME));
	RT_CTRLSTATE nCtrlState =  (!bShowProjectName ? RTCS_DISABLED : 
								(bReadOnly ? RTCS_READONLY : RTCS_ENABLED));
	SetCtrlState(this, IDC_PROJECTNAME, nCtrlState);

	RT_CTRLSTATE nLabelState = (CThemed::IsAppThemed() ? RTCS_ENABLED : RTCS_DISABLED);
	SetCtrlState(this, IDC_PROJECTLABEL, nCtrlState);
}

int CToDoCtrl::CalcMaxCommentSize() const
{
	CRect rClient;
	GetClientRect(rClient);

	switch (m_nCommentsPos)
	{
	case TDCUIL_LEFT:
	case TDCUIL_RIGHT:
		return (rClient.Width() - MINNONCOMMENTWIDTH);

	case TDCUIL_BOTTOM:
		return (rClient.Height() - MINNONCOMMENTHEIGHT);
	}

	// all else
	ASSERT(0);
	return -1;
}

BOOL CToDoCtrl::IsCtrlShowing(const CTRLITEM& ctrl) const
{
	if (m_nMaxState != TDCMS_NORMAL)
		return FALSE;

	// is this a custom control?
	if (CTDCCustomAttributeUIHelper::IsCustomEditControl(ctrl.nCtrlID))
		return TRUE;

	// other special cases
	switch (ctrl.nCtrlID)
	{
	case IDC_COLOUR:
		if (m_visColEdit.GetShowFields() == TDLSA_ASCOLUMN)
		{
			return (!HasStyle(TDCS_COLORTEXTBYATTRIBUTE) &&
					!HasStyle(TDCS_COLORTEXTBYPRIORITY) &&
					!HasStyle(TDCS_COLORTEXTBYNONE));
		}
		break;
	}
	
	// all else
	return m_visColEdit.IsEditFieldVisible(ctrl.nAttrib);
}

void CToDoCtrl::UpdateSelectedTaskPath()
{
	m_taskTree.UpdateSelectedTaskPath();
}

void CToDoCtrl::UpdateControls(BOOL bIncComments, HTREEITEM hti)
{
	if (m_bDeletingTasks)
		return;

#ifdef _DEBUG
	//DWORD dwTick = GetTickCount();
#endif

	if (!hti)
		hti = GetUpdateControlsItem();
	
	BOOL bReadOnly = (IsReadOnly() || !m_taskTree.SelectionHasUnlocked());
	int nSelCount = GetSelectedCount();
	CONTENTFORMAT cfComments;
	
	if (hti)
	{
		DWORD dwTaskID = GetTrueTaskID(hti); 

		BOOL bMaximize = (m_nMaxState != TDCMS_NORMAL);
		BOOL bEnable = (hti && !bMaximize);
		BOOL bIsParent = TSH().ItemsAreAllParents();
		BOOL bAveSubTaskCompletion = HasStyle(TDCS_AVERAGEPERCENTSUBCOMPLETION) && bIsParent;
		BOOL bEditTime = !bIsParent || HasStyle(TDCS_ALLOWPARENTTIMETRACKING);
		BOOL bEditPercent = !HasStyle(TDCS_AUTOCALCPERCENTDONE) && (nSelCount > 1 || !bAveSubTaskCompletion);

		m_nPriority = GetSelectedTaskPriority();
		m_nRisk = GetSelectedTaskRisk();
		m_sAllocBy = GetSelectedTaskAllocBy();
		m_sStatus = GetSelectedTaskStatus();
		m_sExternalID = GetSelectedTaskExtID();
		m_sVersion = GetSelectedTaskVersion();
		m_crColour = GetSelectedTaskColor();

		if (m_crColour == 0)
			m_crColour = CLR_DEFAULT;

		if (bIncComments)
		{
			m_sTextComments = GetSelectedTaskComments();
			m_customComments = GetSelectedTaskCustomComments(cfComments);
		}
		
		CStringArray aMatched, aMixed;
		
		m_taskTree.GetSelectedTaskAllocTo(aMatched, aMixed);
		m_cbAllocTo.SetChecked(aMatched, aMixed);

		m_taskTree.GetSelectedTaskCategories(aMatched, aMixed);
		m_cbCategory.SetChecked(aMatched, aMixed);

		m_taskTree.GetSelectedTaskTags(aMatched, aMixed);
		m_cbTags.SetChecked(aMatched, aMixed);

		// special cases
		GetSelectedTaskFileRefs(m_aFileRefs, FALSE); // relative paths

		CStringArray aDepends;
		GetSelectedTaskDependencies(aDepends);
		m_sDepends = Misc::FormatArray(aDepends);

		if (bEditTime)
		{
			GetSelectedTaskTimeEstimate(m_timeEstimate);
			GetSelectedTaskTimeSpent(m_timeSpent);
		}
		else
		{
			m_timeEstimate.nUnits = m_tdiDefault.timeEstimate.nUnits;
			m_timeEstimate.dAmount = m_calculator.GetTaskTimeEstimate(dwTaskID, m_timeEstimate.nUnits);

			m_timeSpent.nUnits = m_tdiDefault.timeSpent.nUnits;
			m_timeSpent.dAmount = m_calculator.GetTaskTimeSpent(dwTaskID, m_timeEstimate.nUnits);
		}

		// chess clock for time spent
		BOOL bCanTimeTrack = (bEditTime && 
								bEnable && 
								!bReadOnly && 
								(nSelCount == 1) && 
								m_timeTracking.CanTrackTask(dwTaskID));

		BOOL bIsTrackingTask = (bCanTimeTrack && m_timeTracking.IsTrackingTask(dwTaskID));

		m_eTimeSpent.CheckButton(ID_TIME_TRACK, bIsTrackingTask);
		m_eTimeSpent.EnableButton(ID_TIME_TRACK, bCanTimeTrack);
		m_eTimeSpent.EnableButton(ID_ADD_TIME, (bCanTimeTrack && !bIsTrackingTask));

		// dependency link button
		m_eDependency.EnableButton(ID_DEPENDS_LINK, bEnable && !m_sDepends.IsEmpty());

		// percent done
		if (IsSelectedTaskDone())
		{
			m_nPercentDone = 100;
		}
		else if (bEditPercent)
		{
			m_nPercentDone = GetSelectedTaskPercent();
		}
		else
		{
			m_nPercentDone = m_calculator.GetTaskPercentDone(dwTaskID);
		}
		
		// Misc
		GetSelectedTaskCost(m_cost);
		GetSelectedTaskRecurrence(m_tRecurrence);
		GetSelectedTaskCustomAttributeData(m_mapCustomCtrlData, FALSE);
	}
	else // clear controls
	{
		m_nPriority = 0;
		m_nRisk = 0;
		m_nPercentDone = 0;
		m_timeEstimate.dAmount = m_timeSpent.dAmount = 0;
		m_cost.dAmount = 0.0;
		m_tRecurrence = TDCRECURRENCE();
		m_crColour = CLR_DEFAULT;

		m_sTextComments.Empty();
		m_customComments.Empty();
		m_sAllocBy.Empty();
		m_sStatus.Empty();
		m_sExternalID.Empty();
		m_sDepends.Empty();
		m_sVersion.Empty();

		m_cbAllocTo.CheckAll(CCBC_UNCHECKED);
		m_cbCategory.CheckAll(CCBC_UNCHECKED);
		m_cbTags.CheckAll(CCBC_UNCHECKED);

		m_aFileRefs.RemoveAll();

		m_eTimeSpent.EnableButton(ID_TIME_TRACK, FALSE);
		m_eTimeSpent.EnableButton(ID_ADD_TIME, FALSE);
		m_eDependency.EnableButton(ID_DEPENDS_LINK, FALSE);

		m_mapCustomCtrlData.RemoveAll();

		// Keep whatever the current comments contents type is
		if (bIncComments)
			m_ctrlComments.GetSelectedFormat(cfComments);
	}

	UpdateDateTimeControls(hti != NULL);

	// update data controls excluding comments
	UpdateData(FALSE);

	if (bIncComments)
	{
		// if more than one comments type is selected then sCommentsType
		// will be empty which will put the comments type combo in an
		// indeterminate state which is the desired effect since this requires
		// the user to reset the type before they can edit
		if (m_ctrlComments.SetSelectedFormat(cfComments))
			m_cfComments = cfComments;
		else
			m_cfComments.Empty();
		
		UpdateComments(FALSE);
	}

	// and task header
	UpdateSelectedTaskPath();
	
	EnableDisableControls(hti);

#ifdef _DEBUG
//	TRACE(_T("CToDoCtrl::UpdateControls(took %d ms)\n"), (GetTickCount() - dwTick));
#endif
}

void CToDoCtrl::UpdateDateTimeControls(BOOL bHasSelection)
{
	if (bHasSelection)
	{
		COleDateTime dateStart = GetSelectedTaskDate(TDCD_START);
		SetCtrlDate(m_dtcStart, dateStart);
		m_cbTimeStart.SetOleTime(dateStart.m_dt);
		
		COleDateTime dateDue = GetSelectedTaskDate(TDCD_DUE);
		SetCtrlDate(m_dtcDue, dateDue, dateStart);
		m_cbTimeDue.SetOleTime(dateDue.m_dt);
		
		COleDateTime dateDone = GetSelectedTaskDate(TDCD_DONE);
		SetCtrlDate(m_dtcDone, dateDone);
		m_cbTimeDone.SetOleTime(dateDone.m_dt);

		// use due date if present else start date
		if (CDateHelper::IsDateSet(dateDue))
			m_eRecurrence.SetDefaultDate(dateDue);
		else
			m_eRecurrence.SetDefaultDate(dateStart);
	}
	else
	{
		COleDateTime date;
		SetCtrlDate(m_dtcDue, date);
		SetCtrlDate(m_dtcDone, date);
		SetCtrlDate(m_dtcStart, date);

		m_cbTimeStart.SetOleTime(-1);
		m_cbTimeDue.SetOleTime(-1);
		m_cbTimeDone.SetOleTime(-1);
	}
}

void CToDoCtrl::UpdateTasklistVisibility()
{
	BOOL bTasksVis = (m_nMaxState != TDCMS_MAXCOMMENTS);

	// if only the comments are visible then set the focus to the comments
	// before hiding the tasks, else Windows will select all the text
	// in the comments control
	if (!bTasksVis)
		m_ctrlComments.SetFocus();

	m_taskTree.Show(bTasksVis);
}

void CToDoCtrl::SetCtrlDate(CDateTimeCtrl& ctrl, const COleDateTime& date, const COleDateTime& dateMin)
{
	if (CDateHelper::IsDateSet(date))
	{
		ctrl.SetTime(date);
	}
	else
	{
		COleDateTime dtToday = COleDateTime::GetCurrentTime();
		
		if (CDateHelper::IsDateSet(dateMin))
			ctrl.SetTime(max(dateMin, dtToday));
		else
			ctrl.SetTime(dtToday);
		
		ctrl.SendMessage(DTM_SETSYSTEMTIME, GDT_NONE, 0);
	}
}

void CToDoCtrl::UpdateTask(TDC_ATTRIBUTE nAttrib, DWORD dwFlags)
{
	if (!m_taskTree.GetSafeHwnd())
		return;
	
	if (!CanEditSelectedTask(nAttrib))
		return;
	
	// special case to circumvent CSaveFocus else it can mess up IME input
	if (nAttrib == TDCA_COMMENTS)
	{
		CDWordArray aModTaskIDs;
		GetSelectedTaskIDs(aModTaskIDs);

		m_nCommentsState = CS_PENDING;
		SetModified(TDCA_COMMENTS, aModTaskIDs);

		return;
	}	
	
	// else
	CSaveFocus sf;
	UpdateData();
	
	switch (nAttrib)
	{
	case TDCA_DONEDATE:
		{
			COleDateTime date;
			m_dtcDone.GetTime(date);
			
			if (SetSelectedTaskDate(TDCD_DONE, date, TRUE))
			{
				// check if we need to modify percent done also
				if (!IsSelectedTaskDone())
				{
					int nPercentDone = GetSelectedTaskPercent();
					
					if (nPercentDone == 100)
						nPercentDone = 0;
					
					SetSelectedTaskPercentDone(nPercentDone);
					m_nPercentDone = nPercentDone;
					
					UpdateData(FALSE);
				}
				else if (m_nPercentDone != 100) // make the percent field look right
				{
					m_nPercentDone = 100;
					UpdateData(FALSE);
				}
			}
			else
			{
				UpdateControls(FALSE); // don't update comments
			}

			// enable done time field depending on whether the done date is valid or not
			m_cbTimeDone.EnableWindow(CDateHelper::IsDateSet(date));
		}
		break;
		
	case TDCA_DONETIME:
		SetSelectedTaskDate(TDCD_DONETIME, m_cbTimeDone.GetOleTime(), TRUE);
		break;
		
	case TDCA_STARTDATE:
		{
			COleDateTime date;
			m_dtcStart.GetTime(date);
			
			SetSelectedTaskDate(TDCD_STARTDATE, date, TRUE);

			// enable start time field depending on whether the start date is valid or not
			m_cbTimeStart.EnableWindow(CDateHelper::IsDateSet(date));
		}
		break;
		
	case TDCA_STARTTIME:
		SetSelectedTaskDate(TDCD_STARTTIME, m_cbTimeStart.GetOleTime(), TRUE);
		break;
		
	case TDCA_DUEDATE:
		{
			COleDateTime date;
			m_dtcDue.GetTime(date);
			
			SetSelectedTaskDate(TDCD_DUEDATE, date, TRUE);

			// enable due time field depending on whether the due date is valid or not
			m_cbTimeDue.EnableWindow(CDateHelper::IsDateSet(date));
		}
		break;
		
	case TDCA_DUETIME:
		SetSelectedTaskDate(TDCD_DUETIME, m_cbTimeDue.GetOleTime(), TRUE);
		break;
		
	case TDCA_COST:
		SetSelectedTaskCost(m_cost);
		break;
		
	case TDCA_RECURRENCE:
		SetSelectedTaskRecurrence(m_tRecurrence);
		break;
		
	case TDCA_DEPENDENCY:
		{
			CStringArray aDepends;

			Misc::Split(m_sDepends, aDepends);
			SetSelectedTaskDependencies(aDepends);

			m_eDependency.EnableButton(ID_DEPENDS_LINK, !m_sDepends.IsEmpty());
		}
		break;
		
	case TDCA_PRIORITY:
		SetSelectedTaskPriority(m_nPriority);
		break;
		
	case TDCA_RISK:
		SetSelectedTaskRisk(m_nRisk);
		break;
		
	case TDCA_COLOR:
		SetSelectedTaskColor(m_crColour);
		break;
		
	case TDCA_EXTERNALID:
		SetSelectedTaskExternalID(m_sExternalID);
		break;
		
	case TDCA_ALLOCTO:
		SetSelectedTaskArray(TDCA_ALLOCTO, m_cbAllocTo);
		break;
		
	case TDCA_ALLOCBY:
		SetSelectedTaskAllocBy(m_sAllocBy);
		break;
		
	case TDCA_STATUS:
		SetSelectedTaskStatus(m_sStatus);
		break;
		
	case TDCA_VERSION:
		SetSelectedTaskVersion(m_sVersion);
		break;
		
	case TDCA_CATEGORY:
		SetSelectedTaskArray(TDCA_CATEGORY, m_cbCategory);
		break;
		
	case TDCA_TAGS:
		SetSelectedTaskArray(TDCA_TAGS, m_cbTags);
		break;
		
	case TDCA_PERCENT:
		{
			// note: we need to take account of 'done' state too because
			// we maintain the task percent at its pre-done state even
			// if the UI says its '100%'
			BOOL bWasDone = IsSelectedTaskDone();
			SetSelectedTaskPercentDone(m_nPercentDone);
			
			// check if we need to update 'done' state
			BOOL bDoneChange = (bWasDone && m_nPercentDone < 100) || (!bWasDone && m_nPercentDone == 100);
			
			if (bDoneChange)
				SetSelectedTaskDone(m_nPercentDone == 100);
		}
		break;
		
	case TDCA_TIMEEST:
		if (dwFlags & UTF_TIMEUNITSONLY)
			SetSelectedTaskTimeEstimateUnits(m_timeEstimate.nUnits, Misc::HasFlag(dwFlags, UTF_RECALCTIME));
		else
			SetSelectedTaskTimeEstimate(m_timeEstimate);
		break;
		
	case TDCA_TIMESPENT:
		if (dwFlags & UTF_TIMEUNITSONLY)
			SetSelectedTaskTimeSpentUnits(m_timeSpent.nUnits, Misc::HasFlag(dwFlags, UTF_RECALCTIME));
		else
			SetSelectedTaskTimeSpent(m_timeSpent);
		break;
		
	case TDCA_FILEREF:
		if (!m_cbFileRef.GetDroppedState())
		{
			BOOL bAppend = (GetSelectedCount() > 1);
			SetSelectedTaskFileRefs(m_aFileRefs, bAppend, TRUE);
		}
		break;
		
	default:
		// handle custom attributes
		if (TDCCUSTOMATTRIBUTEDEFINITION::IsCustomAttribute(nAttrib))
		{
			CString sAttribID = m_aCustomAttribDefs.GetAttributeTypeID(nAttrib);
			TDCCADATA data;

			if (m_mapCustomCtrlData.Lookup(sAttribID, data))
				SetSelectedTaskCustomAttributeData(sAttribID, data, TRUE);
			else
				ClearSelectedTaskCustomAttributeData(sAttribID, TRUE);
		}
	}
}

void CToDoCtrl::OnChangePriority()
{
	UpdateTask(TDCA_PRIORITY);
}

void CToDoCtrl::OnChangeRisk()
{
	UpdateTask(TDCA_RISK);
}

void CToDoCtrl::OnStartDatechange(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
	// ignore this if the date selector is dropped down
	if (!m_dtcStart.IsCalendarVisible())
		UpdateTask(TDCA_STARTDATE); 
	
	*pResult = 0;
}

void CToDoCtrl::OnDueDatechange(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
	// ignore this if the date selector is dropped down
	if (!m_dtcDue.IsCalendarVisible())
		UpdateTask(TDCA_DUEDATE); 	
	
	*pResult = 0;
}

void CToDoCtrl::OnCompletionDatechange(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
	// ignore this if the date selector is dropped down
	if (!m_dtcDone.IsCalendarVisible())
		UpdateTask(TDCA_DONEDATE);	
	
	*pResult = 0;
}

void CToDoCtrl::OnSelChangeDueTime()
{
	UpdateTask(TDCA_DUETIME);
}

void CToDoCtrl::OnSelChangeStartTime()
{
	UpdateTask(TDCA_STARTTIME);
}

void CToDoCtrl::OnSelChangeDoneTime()
{
	UpdateTask(TDCA_DONETIME);
}

void CToDoCtrl::OnCustomAttributeChange(UINT nCtrlID, NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
	OnCustomAttributeChange(nCtrlID);
	*pResult = 0;
}

void CToDoCtrl::OnCustomAttributeChange(UINT nCtrlID)
{
	ASSERT(CTDCCustomAttributeUIHelper::IsCustomEditControl(nCtrlID));

	CUSTOMATTRIBCTRLITEM ctrl;

	if (CTDCCustomAttributeUIHelper::GetControl(nCtrlID, m_aCustomControls, ctrl))
	{
		UpdateTask(ctrl.nAttrib);
	}
}

void CToDoCtrl::OnCustomAttributeCancel(UINT nCtrlID)
{
	ASSERT(CTDCCustomAttributeUIHelper::IsCustomEditControl(nCtrlID));

	CUSTOMATTRIBCTRLITEM ctrl;

	if (CTDCCustomAttributeUIHelper::GetControl(nCtrlID, m_aCustomControls, ctrl))
	{
		// Restore previous control values
		TDCCADATA data;
		m_mapCustomCtrlData.Lookup(ctrl.sAttribID, data);

		CTDCCustomAttributeUIHelper::UpdateControl(this, ctrl, m_aCustomAttribDefs, data);
	}
}

// external version
BOOL CToDoCtrl::SetSelectedTaskCustomAttributeData(const CString& sAttribID, const TDCCADATA& data)
{
	return SetSelectedTaskCustomAttributeData(sAttribID, data, FALSE);
}

BOOL CToDoCtrl::SetSelectedTaskCustomAttributeData(const CString& sAttribID, const TDCCADATA& data, BOOL bCtrlEdited)
{
	if (!CanEditSelectedTask(TDCA_CUSTOMATTRIB))
		return FALSE;

	if (sAttribID.IsEmpty())
		return FALSE;

	Flush();
	
	POSITION pos = TSH().GetFirstItemPos();
	TDC_SET nRes = SET_NOCHANGE;
	CDWordArray aModTaskIDs;
	
	IMPLEMENT_DATA_UNDO_EDIT(m_data);
		
	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);

		if (!HandleModResult(dwTaskID, m_data.SetTaskCustomAttributeData(dwTaskID, sAttribID, data), aModTaskIDs))
			return FALSE;
	}
	
	if (aModTaskIDs.GetSize())
	{
		TDC_ATTRIBUTE nAttrib = m_aCustomAttribDefs.GetAttributeID(sAttribID);
 		SetModified(nAttrib, aModTaskIDs);

		// update UI except if it's already up to date
		CUSTOMATTRIBCTRLITEM ctrl;
		
		if (CTDCCustomAttributeUIHelper::GetControl(sAttribID, m_aCustomControls, ctrl))
		{
			if (!bCtrlEdited)
				CTDCCustomAttributeUIHelper::UpdateControl(this, ctrl, m_aCustomAttribDefs, data);

			if (ctrl.IsShowingBuddy())
				EnableDisableControls(GetSelectedItem());
		}
	}
	
	return TRUE;
}

BOOL CToDoCtrl::ClearSelectedTaskCustomAttributeData(const CString& sAttribID, BOOL bCtrlEdited)
{
	return SetSelectedTaskCustomAttributeData(sAttribID, TDCCADATA(), bCtrlEdited);
}

BOOL CToDoCtrl::SetSelectedTaskMetaData(const CString& sKey, const CString& sMetaData)
{
	if (!CanEditSelectedTask(TDCA_METADATA))
		return FALSE;

	if (sKey.IsEmpty())
		return FALSE;

	Flush();
	
	IMPLEMENT_DATA_UNDO_EDIT(m_data);
		
	CDWordArray aModTaskIDs;
	POSITION pos = TSH().GetFirstItemPos();
	
	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);

		if (!HandleModResult(dwTaskID, m_data.SetTaskMetaData(dwTaskID, sKey, sMetaData), aModTaskIDs))
			return FALSE;
	}
	
	if (aModTaskIDs.GetSize())
 		SetModified(TDCA_METADATA, aModTaskIDs);
	
	return TRUE;
}

void CToDoCtrl::DrawSplitter(CDC* pDC) 
{
	// draw splitter and clip out
	if ((m_nMaxState == TDCMS_NORMAL) &&
		m_ctrlComments.GetSafeHwnd() && 
		m_theme.IsSet() && 
		IsCommentsVisible())
	{
		CRect rSplitter = GetSplitterRect();
		GraphicsMisc::DrawSplitBar(pDC, rSplitter, m_theme.crAppBackDark, FALSE);

		pDC->ExcludeClipRect(rSplitter);
	}
}

BOOL CToDoCtrl::OnEraseBkgnd(CDC* pDC) 
{
	// if the task tree has no size, we can treat this as spurious
	if (m_nMaxState == TDCMS_NORMAL)
	{
		CRect rTree;
		m_taskTree.GetWindowRect(rTree);
		
		if ((rTree.Width() == 0) && (rTree.Height() == 0))
			return TRUE;
	}

	// clip out all the child controls to reduce flicker
	if (!(GetStyle() & WS_CLIPCHILDREN) && m_taskTree.GetSafeHwnd())
	{
		ExcludeCtrls(pDC, IDC_FIRST + 1, IDC_LAST - 1, TRUE);
		ExcludeChild(&m_taskTree, pDC);
	}
	
	// fill background with theme brush
	int nDC = pDC->SaveDC();
	
	if (m_brUIBack.GetSafeHandle())
	{
		CRect rect;
		pDC->GetClipBox(rect);
		pDC->FillSolidRect(rect, m_theme.crAppBackLight);
	}
	else // default
	{
		CRuntimeDlg::OnEraseBkgnd(pDC);
	}

	// draw comments splitter
	DrawSplitter(pDC);
	
	pDC->RestoreDC(nDC);
	
	return TRUE;
}

int CToDoCtrl::GetAutoListData(TDCAUTOLISTDATA& tld, TDC_ATTRIBUTE nAttribID) const
{
	return tld.Copy(m_tldAll, nAttribID);
}

void CToDoCtrl::UpdateAutoListData(TDC_ATTRIBUTE nAttrib)
{
	BOOL bWantAll = (nAttrib == TDCA_ALL);

	if (bWantAll || (nAttrib == TDCA_CATEGORY))
		m_cbCategory.GetItems(m_tldAll.aCategory); 
	
	if (bWantAll || (nAttrib == TDCA_ALLOCTO))
		m_cbAllocTo.GetItems(m_tldAll.aAllocTo);

	if (bWantAll || (nAttrib == TDCA_ALLOCBY))
		m_cbAllocBy.GetItems(m_tldAll.aAllocBy);

	if (bWantAll || (nAttrib == TDCA_STATUS))
		m_cbStatus.GetItems(m_tldAll.aStatus);

	if (bWantAll || (nAttrib == TDCA_TAGS))
		m_cbTags.GetItems(m_tldAll.aTags);

	if (bWantAll || (nAttrib == TDCA_VERSION))
		m_cbVersion.GetItems(m_tldAll.aVersion);
}

void CToDoCtrl::SetDefaultAutoListData(const TDCAUTOLISTDATA& tld)
{
	// update the combos before copying over the current defaults

	// multi-selection
	SetDefaultListContent(m_cbAllocTo,	tld.aAllocTo,	m_tldDefault.aAllocTo);
	SetDefaultListContent(m_cbCategory,	tld.aCategory,	m_tldDefault.aCategory);
	SetDefaultListContent(m_cbTags,		tld.aTags,		m_tldDefault.aTags);

	// single selection
	SetDefaultListContent(m_cbStatus,	tld.aStatus,	m_tldDefault.aStatus, TRUE);
	SetDefaultListContent(m_cbVersion,	tld.aVersion,	m_tldDefault.aVersion, TRUE);
	SetDefaultListContent(m_cbAllocBy,	tld.aAllocBy,	m_tldDefault.aAllocBy, TRUE);

	// save
	m_tldDefault.Copy(tld, TDCA_ALL);
	UpdateAutoListData();
	
	// restore selection
	OnSelCancelAllocTo();
	OnSelCancelCategory();
	OnSelCancelTag();

	m_cbAllocBy.SelectString(0, m_sAllocBy);
	m_cbStatus.SelectString(0, m_sStatus);
	m_cbVersion.SelectString(0, m_sVersion);
}

void CToDoCtrl::SetDefaultListContent(CAutoComboBox& combo, const CStringArray& aNewDefs, 
									 const CStringArray& aOldDefs, BOOL bAddEmpty)
{
	CHoldRedraw hr(combo);
	
	// if the combo contents are not writable then combo ought to
	// only contain the old defaults so we just replace with the new
	BOOL bIsReadOnly = !CDialogHelper::ComboHasEdit(combo);

	if (bIsReadOnly)
	{
		CDialogHelper::SetComboBoxItems(combo, aNewDefs);
	}
	else
	{
		CStringArray aComboItems;
		CDialogHelper::GetComboBoxItems(combo, aComboItems);

		// remove the old defaults
		Misc::RemoveEmptyItems(aComboItems);
		Misc::RemoveItems(aOldDefs, aComboItems);

		// add new defaults
		Misc::AddUniqueItems(aNewDefs, aComboItems);

		// update combo
		CDialogHelper::SetComboBoxItems(combo, aComboItems);
	}

	// add empty as required
	if (bAddEmpty)
		combo.AddEmptyString();

	// and recalc drop-width
	combo.RefreshDropWidth();
}

BOOL CToDoCtrl::SetAutoListContentReadOnly(TDC_ATTRIBUTE nListAttribID, BOOL bReadOnly)
{
	switch (nListAttribID)
	{
	// multi-selection
	case TDCA_CATEGORY: return SetAutoComboReadOnly(m_cbCategory,	bReadOnly, m_tldDefault.aCategory, FALSE);
	case TDCA_ALLOCTO:	return SetAutoComboReadOnly(m_cbAllocTo,	bReadOnly, m_tldDefault.aAllocTo, FALSE);
	case TDCA_TAGS:		return SetAutoComboReadOnly(m_cbTags,		bReadOnly, m_tldDefault.aTags, FALSE);

	// single selection
	case TDCA_ALLOCBY:	return SetAutoComboReadOnly(m_cbAllocBy,	bReadOnly, m_tldDefault.aAllocBy, TRUE);
	case TDCA_VERSION:	return SetAutoComboReadOnly(m_cbVersion,	bReadOnly, m_tldDefault.aVersion, TRUE);
	case TDCA_STATUS:	return SetAutoComboReadOnly(m_cbStatus,		bReadOnly, m_tldDefault.aStatus, TRUE);
	}

	// all else
	return FALSE;
}

BOOL CToDoCtrl::RenameTaskAttributeValues(TDC_ATTRIBUTE nListAttribID, const CString& sFrom, const CString& sTo, BOOL bCaseSensitive, BOOL bWholeWord)
{
	return (m_data.RenameTasksAttributeValue(nListAttribID, sFrom, sTo, bCaseSensitive, bWholeWord) == SET_CHANGE);
}

BOOL CToDoCtrl::SetAutoComboReadOnly(CAutoComboBox& combo, BOOL bReadOnly, const CStringArray& aDefContent, BOOL bAddEmpty)
{
	BOOL bWasReadOnly = !CDialogHelper::ComboHasEdit(combo);

	CStringArray aContent;
	CDialogHelper::GetComboBoxItems(combo, aContent);

	if (!CDialogHelper::SetAutoComboReadOnly(combo, TRUE, bReadOnly, COMBODROPHEIGHT))
		return FALSE;

	if ((bReadOnly && !bWasReadOnly) || (!bReadOnly && bWasReadOnly))
	{
		CHoldRedraw hr(combo);

		// if switching TO readonly restore to default items
		if (bReadOnly)
			combo.AddUniqueItems(aDefContent);
		else
			combo.AddUniqueItems(aContent);
	
		// and recalc drop-width
		combo.RefreshDropWidth();

		if (bAddEmpty)
			combo.AddEmptyString();

		m_mgrPrompts.SetComboPrompt(combo, IDS_TDC_EDITPROMPT_NAME);

		// restore selected task items
		UpdateData(FALSE);
	}

	return TRUE;
}

void CToDoCtrl::NewList()
{
	Flush();

	// Temporarily disable delete confirmation
	BOOL bConfirmDelete = HasStyle(TDCS_CONFIRMDELETE);
	
	if (bConfirmDelete)
		m_styles[TDCS_CONFIRMDELETE] = FALSE;
	
	DeleteAllTasks();
	
	if (bConfirmDelete)
		m_styles[TDCS_CONFIRMDELETE] = TRUE;

	m_sProjectName.Empty();
	m_nFileVersion = 0;
	m_bModified = FALSE;
	m_sPassword.Empty();
	
	UpdateData(FALSE);
	UpdateComments(FALSE);
}

BOOL CToDoCtrl::SetSelectedTaskColor(COLORREF color)
{
	if (!CanEditSelectedTask(TDCA_COLOR))
		return FALSE;

	Flush();
	
	IMPLEMENT_DATA_UNDO_EDIT(m_data);
		
	CDWordArray aModTaskIDs;
	POSITION pos = TSH().GetFirstItemPos();
	
	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);
		
		if (!HandleModResult(dwTaskID, m_data.SetTaskColor(dwTaskID, color), aModTaskIDs))
			return FALSE;
	}
	
	if (aModTaskIDs.GetSize())
	{
		if (color == CLR_NONE)
		{
			m_cpColour.SetBkColour(CLR_DEFAULT);
			m_cpColour.SetTextColour(CLR_DEFAULT);
		}
		else
		{
			m_cpColour.SetColour(color);
		}

		SetModified(TDCA_COLOR, aModTaskIDs);
	}
	
	return TRUE;
}

BOOL CToDoCtrl::EditSelectedTaskIcon()
{
	if (!CanEditSelectedTask(TDCA_ICON))
		return FALSE;

	CTDLTaskIconDlg dialog(m_ilTaskIcons, GetSelectedTaskIcon(), TRUE, this);

	if (dialog.DoModal() != IDOK)
		return FALSE;

	return SetSelectedTaskIcon(dialog.GetIconName());
}

LRESULT CToDoCtrl::OnTaskIconDlgReloadIcons(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	if (m_ilTaskIcons.LoadImages(m_sLastSavePath, MAGENTA, HasStyle(TDCS_SHOWDEFAULTTASKICONS)))
	{
		OnTaskIconsChanged();
		return TRUE;
	}

	// else 
	return FALSE;
}

BOOL CToDoCtrl::ClearSelectedTaskIcon()
{
	if (!CanEditSelectedTask(TDCA_ICON))
		return FALSE;

	return SetSelectedTaskIcon(_T(""));
}

BOOL CToDoCtrl::SetSelectedTaskIcon(const CString& sIcon)
{
	Flush();
	
	IMPLEMENT_DATA_UNDO_EDIT(m_data);
		
	CDWordArray aModTaskIDs;
	POSITION pos = TSH().GetFirstItemPos();
	
	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);

		if (!HandleModResult(dwTaskID, m_data.SetTaskIcon(dwTaskID, sIcon), aModTaskIDs))
			return FALSE;
	}
	
	if (aModTaskIDs.GetSize())
		SetModified(TDCA_ICON, aModTaskIDs);
	
	return TRUE;
}

BOOL CToDoCtrl::CanPasteText()
{
	TDC_ATTRIBUTE nAttribID = GetFocusedControlAttribute();
	
	return (CanEditSelectedTask(nAttribID) && 
			((nAttribID == TDCA_COMMENTS) || CWinClasses::IsEditControl(::GetFocus())));
}

BOOL CToDoCtrl::PasteText(const CString& sText)
{
	if (!CanPasteText() || sText.IsEmpty())
		return FALSE;

	if (GetFocusedControlAttribute() == TDCA_COMMENTS)
		return m_ctrlComments.InsertTextContent(sText, FALSE);

	// else
	HWND hFocus = ::GetFocus();
	::SendMessage(hFocus, EM_REPLACESEL, TRUE, (LPARAM)(LPCTSTR)sText);
	
	return TRUE;
}

BOOL CToDoCtrl::SetSelectedTaskComments(const CString& sComments, const CBinaryData& customComments)
{
	return SetSelectedTaskComments(sComments, customComments, FALSE); 
}

BOOL CToDoCtrl::SetSelectedTaskComments(const CString& sComments, const CBinaryData& customComments, BOOL bInternal)
{
	if (!CanEditSelectedTask(TDCA_COMMENTS))
		return FALSE;

	if (!bInternal)
		Flush();
	
	IMPLEMENT_DATA_UNDO_EDIT(m_data);
		
	CDWordArray aModTaskIDs;
	POSITION pos = TSH().GetFirstItemPos();

	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);

		if (!HandleModResult(dwTaskID, m_data.SetTaskComments(dwTaskID, sComments, customComments), aModTaskIDs))
			return FALSE;
	}
	
	if (aModTaskIDs.GetSize())
	{
		// refresh the comments of the active task if we were called externally
		// note: we don't use SetTextChange because that doesn't handle custom comments
		if (!bInternal && (TSH().GetCount() == 1))
		{
			m_sTextComments = GetSelectedTaskComments();
			m_customComments = GetSelectedTaskCustomComments(m_cfComments);

			UpdateComments(FALSE);
		}

		TSH().InvalidateAll();

		SetModified(TDCA_COMMENTS, aModTaskIDs);
	}
	
	return TRUE;
}

BOOL CToDoCtrl::SetSelectedTaskTitle(const CString& sTitle, BOOL bAllowMultiple)
{
	if (sTitle.IsEmpty()) // sanity check
		return FALSE;

	if (!CanEditSelectedTask(TDCA_TASKNAME))
		return FALSE;

	if (!bAllowMultiple && (GetSelectedCount() > 1))
		return FALSE;

	Flush();
	
	IMPLEMENT_DATA_UNDO_EDIT(m_data);
		
	CDWordArray aModTaskIDs;
	POSITION pos = TSH().GetFirstItemPos();

	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);

		if (!HandleModResult(dwTaskID, m_data.SetTaskTitle(dwTaskID, sTitle), aModTaskIDs))
			return FALSE;
	}

	if (aModTaskIDs.GetSize())
	{
		m_taskTree.Tree().SetItemText(GetSelectedItem(), sTitle);
		m_taskTree.InvalidateSelection();

		SetModified(TDCA_TASKNAME, aModTaskIDs);
	}
	
	return TRUE;
}

BOOL CToDoCtrl::GetSelectionBoundingRect(CRect& rSelection) const
{
	if (m_taskTree.GetSelectionBoundingRect(rSelection))
	{
		m_taskTree.ClientToScreen(rSelection);
		ScreenToClient(rSelection);

		return TRUE;
	}

	return FALSE;
}

COleDateTime CToDoCtrl::GetTaskDate(DWORD dwTaskID, TDC_DATE nDate) const
{
	return m_data.GetTaskDate(dwTaskID, nDate);
}

BOOL CToDoCtrl::GetTaskTimes(DWORD dwTaskID, TDCTIMEPERIOD& timeEst, TDCTIMEPERIOD& timeSpent) const
{
	if (!m_data.HasTask(dwTaskID))
		return FALSE;

	m_data.GetTaskTimeEstimate(dwTaskID, timeEst);
	m_data.GetTaskTimeSpent(dwTaskID, timeSpent);

	return TRUE;
}

BOOL CToDoCtrl::SetSelectedTaskPriority(int nPriority, BOOL bOffset)
{
	if (!CanEditSelectedTask(TDCA_PRIORITY))
		return FALSE;

	Flush();

	IMPLEMENT_DATA_UNDO_EDIT(m_data);
		
	CDWordArray aModTaskIDs;
	POSITION pos = TSH().GetFirstItemPos();

	// Keep track of what we've processed to avoid offsetting
	// the same task multiple times via references
	CDWordSet mapProcessed;

	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);

		if (bOffset && mapProcessed.Has(dwTaskID))
			continue;

		if (!HandleModResult(dwTaskID, m_data.SetTaskPriority(dwTaskID, nPriority, bOffset), aModTaskIDs))
			return FALSE;

		if (bOffset)
			mapProcessed.Add(dwTaskID);
	}
	
	if (aModTaskIDs.GetSize())
	{
		if (bOffset)
			nPriority = GetSelectedTaskPriority();

		if (m_nPriority != nPriority)
		{
			m_nPriority = nPriority;
			m_cbPriority.SetSelectedPriority(m_nPriority);
		}
		
		SetModified(TDCA_PRIORITY, aModTaskIDs);
	}

	return TRUE;
}

BOOL CToDoCtrl::SetSelectedTaskRisk(int nRisk, BOOL bOffset)
{
	if (!CanEditSelectedTask(TDCA_RISK))
		return FALSE;

	Flush();
	
	IMPLEMENT_DATA_UNDO_EDIT(m_data);
		
	CDWordArray aModTaskIDs;
	POSITION pos = TSH().GetFirstItemPos();
	
	// Keep track of what we've processed to avoid offsetting
	// the same task multiple times via references
	CDWordSet mapProcessed;

	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);

		if (bOffset && mapProcessed.Has(dwTaskID))
			continue;

		if (!HandleModResult(dwTaskID, m_data.SetTaskRisk(dwTaskID, nRisk, bOffset), aModTaskIDs))
			return FALSE;

		if (bOffset)
			mapProcessed.Add(dwTaskID);
	}
	
	if (aModTaskIDs.GetSize())
	{
		if (bOffset)
			nRisk = GetSelectedTaskRisk();

		if (m_nRisk != nRisk)
		{
			m_nRisk = nRisk;
			m_cbRisk.SetSelectedRisk(m_nRisk);
		}
		
		SetModified(TDCA_RISK, aModTaskIDs);
	}
	
	return TRUE;
}

BOOL CToDoCtrl::SetSelectedTaskFlag(BOOL bFlagged)
{
	if (!CanEditSelectedTask(TDCA_FLAG))
		return FALSE;

	Flush();
	
	IMPLEMENT_DATA_UNDO_EDIT(m_data);
		
	CDWordArray aModTaskIDs;
	POSITION pos = TSH().GetFirstItemPos();

	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);

		if (!HandleModResult(dwTaskID, m_data.SetTaskFlag(dwTaskID, bFlagged), aModTaskIDs))
			return FALSE;
	}
	
	if (aModTaskIDs.GetSize())
		SetModified(TDCA_FLAG, aModTaskIDs);
	
	return TRUE;
}

BOOL CToDoCtrl::SetSelectedTaskLock(BOOL bLocked)
{
	if (!CanEditSelectedTask(TDCA_LOCK))
		return FALSE;

	Flush();
	
	IMPLEMENT_DATA_UNDO_EDIT(m_data);
		
	CDWordArray aModTaskIDs;
	POSITION pos = TSH().GetFirstItemPos();

	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);

		if (!HandleModResult(dwTaskID, m_data.SetTaskLock(dwTaskID, bLocked), aModTaskIDs))
			return FALSE;
	}
	
	if (aModTaskIDs.GetSize())
		SetModified(TDCA_LOCK, aModTaskIDs);

	return TRUE;
}

BOOL CToDoCtrl::IncrementSelectedTaskPriority(BOOL bUp)
{
	return SetSelectedTaskPriority((bUp ? 1 : -1), TRUE); // offset
}

void CToDoCtrl::ShowTaskHasIncompleteDependenciesError(const CString& sIncomplete)
{
	if (GetSelectedCount() == 1)
	{
		int nRet = AfxMessageBox(CEnString(IDS_TDC_SELTASKHASDEPENDENCY), MB_YESNO | MB_ICONEXCLAMATION);
		
		if (nRet == IDYES)
			ShowTaskLink(sIncomplete, FALSE);
	}
	else
	{
		AfxMessageBox(CEnString(IDS_TDC_SELTASKSHAVEDEPENDENCIES), MB_OK | MB_ICONEXCLAMATION);
	}
}

void CToDoCtrl::ShowTaskHasCircularDependenciesError(const CDWordArray& aTaskIDs) const
{
	if (GetSelectedCount() == 1)
	{
		AfxMessageBox(CEnString(IDS_TDC_SELTASKHASCIRCULARDEPENDENCY), MB_OK | MB_ICONEXCLAMATION);
	}
	else
	{
		CEnString sMessage(IDS_TDC_SELTASKSHAVECIRCULARDEPENDENCIES, Misc::FormatArray(aTaskIDs));
		AfxMessageBox(sMessage, MB_OK | MB_ICONEXCLAMATION);
	}
}

// external version
BOOL CToDoCtrl::SetSelectedTaskDate(TDC_DATE nDate, const COleDateTime& date)
{
	return SetSelectedTaskDate(nDate, date, FALSE);
}

// internal version
BOOL CToDoCtrl::SetSelectedTaskDate(TDC_DATE nDate, const COleDateTime& date, BOOL bDateEdited)
{
	// if this is a start/due edit then it must be a component 
	if (bDateEdited && ((nDate == TDCD_DUE) || (nDate == TDCD_START)))
	{
		ASSERT(0);
		return FALSE;
	}

	// special case
	if (nDate == TDCD_DONE)
		return SetSelectedTaskDone(date, bDateEdited);

	TDC_ATTRIBUTE nAttribID = TDC::MapDateToAttribute(nDate);

	if (!CanEditSelectedTask(nAttribID))
		return FALSE;

	IMPLEMENT_DATA_UNDO_EDIT(m_data);
		
	CDWordArray aModTaskIDs;
	POSITION pos = TSH().GetFirstItemPos();

	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);

		// due, start, creation
		if (!HandleModResult(dwTaskID, m_data.SetTaskDate(dwTaskID, nDate, date), aModTaskIDs))
			return FALSE;
	}
	
	if (aModTaskIDs.GetSize())
	{
		BOOL bUpdateTimeEst = FALSE;

		switch (nDate)
		{
		case TDCD_CREATE:	
			SetModified(TDCA_CREATIONDATE, aModTaskIDs); 
			break;

		case TDCD_STARTDATE:
			// update due date ctrl to be start date if 
			// no due date has been specified
			if (bDateEdited && CDateHelper::IsDateSet(date))
			{
				COleDateTime dtDue = GetSelectedTaskDate(TDCD_DUE);

				if (!CDateHelper::IsDateSet(dtDue))
				{
					SetCtrlDate(m_dtcDue, 0.0, date);
				}
			}
			// fall thru

		case TDCD_START:
		case TDCD_STARTTIME:
			bUpdateTimeEst = HasStyle(TDCS_SYNCTIMEESTIMATESANDDATES);

			SetModified(TDCA_STARTDATE, aModTaskIDs); 
			break;
			
		case TDCD_DUEDATE:	
			// update due date ctrl to be start date if 
			// no due date has been specified
			if (bDateEdited && !CDateHelper::IsDateSet(date))
			{
				COleDateTime dtStart = GetSelectedTaskDate(TDCD_START);

				SetCtrlDate(m_dtcDue, 0.0, dtStart);
			}
			m_eRecurrence.SetDefaultDate(date);
			// fall thru

		case TDCD_DUE:
		case TDCD_DUETIME:
			bUpdateTimeEst = HasStyle(TDCS_SYNCTIMEESTIMATESANDDATES);
			
			SetModified(TDCA_DUEDATE, aModTaskIDs); 
			break;
				
		case TDCD_DONETIME:	
			SetModified(TDCA_DONEDATE, aModTaskIDs); 
			break;

		//case TDCD_DONE:
		default:
			ASSERT(0);
			return FALSE;
		}

		// only update controls if the date was changed implicitly
		if (!bDateEdited)
		{
			UpdateControls(FALSE); // don't update comments
		}
		else if (bUpdateTimeEst)
		{
			TDCTIMEPERIOD time;

			if (GetSelectedTaskTimeEstimate(time))
				CTDCDialogHelper().UpdateDataEx(this, m_eTimeEstimate, time, FALSE, DECIMALS);
		}
	}
	
	return TRUE;
}

BOOL CToDoCtrl::OffsetSelectedTaskDate(TDC_DATE nDate, int nAmount, TDC_UNITS nUnits, BOOL bAndSubtasks)
{
	TDC_ATTRIBUTE nAttribID = TDC::MapDateToAttribute(nDate);

	if (!CanEditSelectedTask(nAttribID))
		return FALSE;

	Flush();

	IMPLEMENT_DATA_UNDO_EDIT(m_data);

	// remove duplicate subtasks if we're going to be 
	// processing subtasks anyway
	CHTIList htiSel;
	TSH().CopySelection(htiSel, bAndSubtasks);

	CDWordArray aModTaskIDs;
	POSITION pos = htiSel.GetHeadPosition();
	
	// Keep track of what we've processed to avoid offsetting
	// the same task multiple times via references
	CDWordSet mapProcessed;
	
	while (pos)
	{
		DWORD dwTaskID = GetTrueTaskID(htiSel.GetNext(pos));

		if (mapProcessed.Has(dwTaskID))
			continue;

		if (!HandleModResult(dwTaskID, m_data.OffsetTaskDate(dwTaskID, nDate, nAmount, nUnits, bAndSubtasks, FALSE), aModTaskIDs))
			return FALSE;

		mapProcessed.Add(dwTaskID);
	}
	
	if (aModTaskIDs.GetSize())
	{
		switch (nDate)
		{
		case TDCD_CREATE:	
		case TDCD_START:	
		case TDCD_DUE:		
		case TDCD_DONE:
		case TDCD_STARTDATE:
		case TDCD_DUEDATE:
		case TDCD_DONEDATE:
		case TDCD_STARTTIME:
		case TDCD_DUETIME:
		case TDCD_DONETIME:
			SetModified(nAttribID, aModTaskIDs);
			break;

		default:
			ASSERT(0);
			return FALSE;
		}

		UpdateControls(FALSE); // don't update comments
	}

	return TRUE;
}

BOOL CToDoCtrl::CanOffsetSelectedTaskStartAndDueDates() const
{
	if (!CanEditSelectedTask(TDCA_STARTDATE))
		return FALSE;
	
	if (m_taskTree.SelectionHasDependencies() && HasStyle(TDCS_AUTOADJUSTDEPENDENCYDATES))
		return FALSE;

	// else
	return TRUE;
}

BOOL CToDoCtrl::OffsetSelectedTaskStartAndDueDates(int nAmount, TDC_UNITS nUnits, BOOL bAndSubtasks)
{
	if (!CanEditSelectedTask(TDCA_STARTDATE))
		return FALSE;
	
	Flush();
	
	IMPLEMENT_DATA_UNDO_EDIT(m_data);
	
	// remove duplicate subtasks if we're going to be 
	// processing subtasks anyway
	CHTIList htiSel;
	TSH().CopySelection(htiSel, bAndSubtasks);

	CDWordArray aModTaskIDs;
	POSITION pos = htiSel.GetHeadPosition();

	// Keep track of what we've processed to avoid offsetting
	// the same task multiple times via references
	CDWordSet mapProcessed;
	
	while (pos)
	{
		DWORD dwTaskID = GetTrueTaskID(htiSel.GetNext(pos));

		if (!HandleModResult(dwTaskID, OffsetTaskStartAndDueDates(dwTaskID, nAmount, nUnits, bAndSubtasks, mapProcessed), aModTaskIDs))
			return FALSE;
	}
	
	if (aModTaskIDs.GetSize())
	{
		CTDCAttributeMap mapAttribIDs;
		mapAttribIDs.Add(TDCA_STARTDATE);
		mapAttribIDs.Add(TDCA_DUEDATE);

		SetModified(mapAttribIDs, aModTaskIDs, TRUE); 
		UpdateControls(FALSE); // don't update comments
	}
	
	return TRUE;
}

TDC_SET CToDoCtrl::OffsetTaskStartAndDueDates(DWORD dwTaskID, int nAmount, TDC_UNITS nUnits, BOOL bAndSubtasks, CDWordSet& mapProcessed)
{
	ASSERT(CanEditSelectedTask(TDCA_STARTDATE));

	if (mapProcessed.Has(dwTaskID))
		return SET_NOCHANGE;

	if (m_calculator.IsTaskLocked(dwTaskID))
		return SET_FAILED;

	const TODOITEM* pTDI = GetTask(dwTaskID);

	if (!pTDI)
	{
		ASSERT(0);
		return SET_FAILED;
	}

	TDC_SET nRes = SET_NOCHANGE;

	// Fallback if either start or due date is not set
	if (!pTDI->HasStart())
	{
		// Handle subtasks at the end
		if (pTDI->HasDue())
			nRes = m_data.OffsetTaskDate(dwTaskID, TDCD_DUE, nAmount, nUnits, FALSE, FALSE);
	}
	else if (!pTDI->HasDue())
	{
		// Handle subtasks at the end
		nRes = m_data.OffsetTaskDate(dwTaskID, TDCD_START, nAmount, nUnits, FALSE, FALSE);
	}
	else // else both are set
	{
		COleDateTime dtStart = pTDI->dateStart;
		CDateHelper().OffsetDate(dtStart, nAmount, TDC::MapUnitsToDHUnits(nUnits));

		nRes = m_data.MoveTaskStartAndDueDates(dwTaskID, dtStart);
	}
	ASSERT(nRes != SET_FAILED);

	mapProcessed.Add(dwTaskID);

	// subtasks
	if (bAndSubtasks)
	{
		const TODOSTRUCTURE* pTDS = m_data.LocateTask(dwTaskID);
		ASSERT(pTDS);

		if (pTDS)
		{
			for (int nSubTask = 0; nSubTask < pTDS->GetSubTaskCount(); nSubTask++)
			{
				DWORD dwChildID = pTDS->GetSubTaskID(nSubTask);

				// RECURSIVE CALL
				if (SET_CHANGE == OffsetTaskStartAndDueDates(dwChildID, nAmount, nUnits, TRUE, mapProcessed))
					nRes = SET_CHANGE;
			}
		}
	}

	return nRes;
}

void CToDoCtrl::SetInheritedParentAttributes(const CTDCAttributeMap& mapAttribs, BOOL bUpdateAttrib)
{
	m_data.SetInheritedParentAttributes(mapAttribs, bUpdateAttrib);
}

int CToDoCtrl::CheckWantSubtasksCompleted()
{
	if (m_taskTree.SelectionHasIncompleteSubtasks(TRUE))
	{
		// ask the user what to do
		UINT nIDMessage = (GetSelectedCount() == 1) ? 
							IDS_TDC_SELTASKHASINCOMPLETE : IDS_TDC_TASKHASINCOMPLETE;
		
		int nRet = AfxMessageBox(CEnString(nIDMessage), MB_YESNOCANCEL | MB_ICONQUESTION);

		if (nRet == IDYES)
		{
			return 1;
		}
		else if (nRet == IDCANCEL)
		{
			return -1;
		}
	}

	return 0; // no incomplete subtasks or IDNO
}

BOOL CToDoCtrl::SetSelectedTaskDone(BOOL bDone)
{
	COleDateTime date;

	if (bDone)
		date = COleDateTime::GetCurrentTime();

	return SetSelectedTaskDone(date, FALSE);
}

void CToDoCtrl::AdjustNewRecurringTasksDates(DWORD dwPrevTaskID, DWORD dwNewTaskID, 
											 const COleDateTime& dtNext, BOOL bDueDate)
{
	// we need to move both the due date and the start date forward
	// so we first cache the old dates
	COleDateTime dtStart = m_data.GetTaskDate(dwPrevTaskID, TDCD_START);
	COleDateTime dtDue = m_data.GetTaskDate(dwPrevTaskID, TDCD_DUE);

	BOOL bHasStart = CDateHelper::IsDateSet(dtStart);
	BOOL bHasDue = CDateHelper::IsDateSet(dtDue);

	BOOL bWantInheritStart = m_data.WantUpdateInheritedAttibute(TDCA_STARTDATE);
	BOOL bWantInheritDue = m_data.WantUpdateInheritedAttibute(TDCA_DUEDATE);
	
	if (bDueDate) // dtNext is the new due date
	{
		int nOffsetDays = (bHasDue ? ((int)dtNext - (int)dtDue) : 0);
		
		if (bWantInheritDue)
		{
			m_data.SetTaskDate(dwNewTaskID, TDCD_DUE, dtNext);
			m_data.ApplyLastChangeToSubtasks(dwNewTaskID, TDCA_DUEDATE);
		}
		else // bump dates by required amount
		{
			if (bHasDue)
			{
				// Before we offset, make sure all subtasks have valid due dates
				// And make sure the new date fits the recurring scheme
				m_data.InitMissingTaskDate(dwNewTaskID, TDCD_DUEDATE, dtDue, TRUE);
				m_data.OffsetTaskDate(dwNewTaskID, TDCD_DUEDATE, nOffsetDays, TDCU_DAYS, TRUE, TRUE);
			}
			else
			{
				m_data.InitMissingTaskDate(dwNewTaskID, TDCD_DUE, dtNext, TRUE);
			}
		}

		// adjust start dates similarly
		if (bHasStart)
		{
			// BUT DON'T FIT THE NEW DATE TO THE RECURRING SCHEME
			if (bWantInheritStart)
			{
				// don't offset children
				m_data.OffsetTaskDate(dwNewTaskID, TDCD_STARTDATE, nOffsetDays, TDCU_DAYS, FALSE, FALSE);
				m_data.ApplyLastChangeToSubtasks(dwNewTaskID, TDCA_STARTDATE);
			}
			else // offset children
			{
				// Before we offset, make sure all subtasks have valid start dates
				m_data.InitMissingTaskDate(dwNewTaskID, TDCD_STARTDATE, dtStart, TRUE);
				m_data.OffsetTaskDate(dwNewTaskID, TDCD_STARTDATE, nOffsetDays, TDCU_DAYS, TRUE, FALSE);
			}
		}
	}
	else // dtNext is the new start date
	{
		int nOffsetDays = (bHasStart ? ((int)dtNext - (int)dtStart) : 0);

		if (bWantInheritStart)
		{
			m_data.SetTaskDate(dwNewTaskID, TDCD_START, dtNext);
			m_data.ApplyLastChangeToSubtasks(dwNewTaskID, TDCA_STARTDATE);
		}
		else // bump dates by required amount
		{
			if (bHasStart)
			{
				// Before we offset, make sure all subtasks have valid start dates
				// And make sure the new date fits the recurring scheme
				m_data.InitMissingTaskDate(dwNewTaskID, TDCD_STARTDATE, dtStart, TRUE);
				m_data.OffsetTaskDate(dwNewTaskID, TDCD_STARTDATE, nOffsetDays, TDCU_DAYS, TRUE, TRUE);
			}
			else
			{
				m_data.InitMissingTaskDate(dwNewTaskID, TDCD_START, dtNext, TRUE);
			}
		}

		// adjust due dates similarly
		if (bHasDue)
		{
			// BUT DON'T FIT THE NEW DATE TO THE RECURRING SCHEME
			if (bWantInheritDue)
			{
				// don't update children
				m_data.OffsetTaskDate(dwNewTaskID, TDCD_DUEDATE, nOffsetDays, TDCU_DAYS, FALSE, FALSE);
				m_data.ApplyLastChangeToSubtasks(dwNewTaskID, TDCA_DUEDATE);
			}
			else // bump
			{
				// Before we offset, make sure all subtasks have valid due dates
				m_data.InitMissingTaskDate(dwNewTaskID, TDCD_DUEDATE, dtDue, TRUE);
				m_data.OffsetTaskDate(dwNewTaskID, TDCD_DUEDATE, nOffsetDays, TDCU_DAYS, TRUE, FALSE);
			}
		}
	}
}

BOOL CToDoCtrl::SetSelectedTaskDone(const COleDateTime& date, BOOL bDateEdited)
{
	ASSERT(m_aRecreateTaskIDs.GetSize() == 0);

	if (!CanEditSelectedTask(TDCA_DONEDATE))
		return FALSE;

	Flush();

	// Check for incomplete or circular dependencies
	// and if the user also wants to complete any incomplete subtasks
	BOOL bDone = CDateHelper::IsDateSet(date);
	BOOL bAndSubtasks = FALSE;

	if (bDone && TSH().HasUncheckedItems())
	{
		// check for circular dependencies
		CDWordArray aTaskIDs;
		
		if (m_taskTree.SelectionHasCircularDependencies(aTaskIDs))
		{
			// reset ctrl date
			SetCtrlDate(m_dtcDone, 0.0);
			
			// show error msg
			ShowTaskHasCircularDependenciesError(aTaskIDs);
			
			return FALSE;
		}

		// check for incomplete dependents
		CString sIncomplete;

		if (m_taskTree.SelectionHasIncompleteDependencies(sIncomplete))
		{
			ShowTaskHasIncompleteDependenciesError(sIncomplete);
			return FALSE;
		}

		int nRet = CheckWantSubtasksCompleted();

		if (nRet == -1)
			return FALSE; // cancel

		bAndSubtasks = (nRet != 0);
	}

	// copy the selection and remove child duplicates
	// if setting state to 'done'
	CHTIList selection;
	TSH().CopySelection(selection, bDone);

	IMPLEMENT_DATA_UNDO_EDIT(m_data);
		
	CDWordArray aModTaskIDs;
	BOOL bSomeRecurred = FALSE, bSomeDone = FALSE;

	POSITION pos = selection.GetHeadPosition();

	while (pos)
	{
		HTREEITEM hti = selection.GetNext(pos);

		DWORD dwTaskID = GetTaskID(hti);
		BOOL bWasDone = m_data.IsTaskDone(dwTaskID);

		// Handle recurring tasks, but only if changing state to completed
		COleDateTime dtNext;
		BOOL bRecurring = FALSE, bReuseTask = FALSE, bDueDate = TRUE;
		COleDateTime dtDone(date);
		
		if (bDone && !bWasDone)
		{
			TDCRECURRENCE tr;
			bRecurring = (m_data.CanTaskRecur(dwTaskID) && m_data.GetTaskRecurrence(dwTaskID, tr));

			if (bRecurring)
			{
				switch (tr.nReuse)
				{
				case TDIRO_REUSE:
					bReuseTask = TRUE;
					break;

				case TDIRO_CREATE:
					bReuseTask = FALSE;
					break;

				case TDIRO_ASK:
					{
						CTDLReuseRecurringTaskDlg dialog(tr.bPreserveComments);

						if (dialog.DoModal() == IDCANCEL)
							return FALSE;

						bReuseTask = dialog.GetWantReuseTask();

						// Update the task's 'preserve comments' flag
						// for later initialisation
						tr.bPreserveComments = dialog.GetWantPreserveComments();
						m_data.SetTaskRecurrence(dwTaskID, tr);
					}
					break;
				}
			}
			else
			{
				bSomeDone = TRUE;
			}
		}
		else
		{
			bSomeDone = TRUE;
		}

		// FALSE == Don't update the dates of any already-completed subtasks
		TDC_SET nItemRes = SetTaskDone(dwTaskID, dtDone, bAndSubtasks, FALSE, FALSE);

		if (nItemRes == SET_CHANGE)
		{
			// handle recreation of recurring task
			if (bRecurring)
			{
				if (bReuseTask)
				{
					bSomeRecurred = TRUE;

					VERIFY (m_data.GetTaskNextOccurrence(dwTaskID, dtNext, bDueDate));

					InitialiseNewRecurringTask(dwTaskID, dwTaskID, dtNext, bDueDate);

					// notify parent
					GetParent()->SendMessage(WM_TDCN_RECREATERECURRINGTASK, dwTaskID, dwTaskID);
				}
				else // save off task ID for later recreation
				{
					bSomeDone = TRUE;

					m_aRecreateTaskIDs.Add(dwTaskID);
				}
			}

			aModTaskIDs.Add(dwTaskID);
		}
	}
	
	// update related UI elements
	if (aModTaskIDs.GetSize())
	{
		// only update controls if the date was changed implicitly
		if (!bDateEdited)
			UpdateControls(FALSE); // don't update comments

		m_taskTree.Tree().UpdateWindow();

		// clear done date control if reusing
		if (!bDone || bSomeRecurred)
		{
			SetCtrlDate(m_dtcDone, 0.0);
			m_cbTimeDone.SetOleTime(0.0);
		}
		else if (bDateEdited)
		{
			m_cbTimeDone.SetOleTime(date);
		}

		// if some tasks have recurred then we also need to report 
		// that the start/due dates have changed and selectively
		// update their state
		CTDCAttributeMap mapAttribIDs;

		// if some tasks have recurred or some tasks were completed
		// then we also need to report that the done date has changed
		if (bSomeDone || bSomeRecurred)
			GetAttributesAffectedByMod(TDCA_DONEDATE, mapAttribIDs);

		if (bSomeRecurred)
		{
			COleDateTime dtStart(GetSelectedTaskDate(TDCD_START));
			COleDateTime dtDue(GetSelectedTaskDate(TDCD_DUE));

			SetCtrlDate(m_dtcStart, dtStart);
			SetCtrlDate(m_dtcDue, dtDue);

			m_cbTimeStart.SetOleTime(dtStart.m_dt);
			m_cbTimeDue.SetOleTime(dtDue.m_dt);
		}

		// if some were completed and the status also changed
		// we may also need to update the UI
		if (bSomeDone && !m_sCompletionStatus.IsEmpty())
		{
			m_cbStatus.AddUniqueItem(m_sCompletionStatus);
			UpdateDataEx(this, IDC_STATUS, m_cbStatus, FALSE);
		}
		
		// If some of the tasks were recurring and need to be created
		// we do that after this operation ends
		if (m_aRecreateTaskIDs.GetSize())
		{
			ASSERT(bSomeDone);
			PostMessage(WM_TDC_RECREATERECURRINGTASK, 0, m_aRecreateTaskIDs.GetSize());
		}

		SetModified(mapAttribIDs, aModTaskIDs, TRUE);
	}
	
	return TRUE;
}

LRESULT CToDoCtrl::OnRecreateRecurringTask(WPARAM /*wParam*/, LPARAM lParam)
{
	int nNumTasks = lParam;

	// sanity checks
	if (!nNumTasks || (nNumTasks != m_aRecreateTaskIDs.GetSize()))
	{
		ASSERT(0);
		m_aRecreateTaskIDs.RemoveAll();
		return 0L;
	}

	// Always extend the previous undo action which completed
	// the recurring task(s) we are about to recreate
	IMPLEMENT_DATA_UNDO_EXTEND(m_data, TDCUAT_ADD, TRUE);

	CDWordArray aTaskIDs, aNewTaskIDs;

	for (int nTask = 0; nTask < nNumTasks; nTask++)
	{
		DWORD dwTaskID = m_aRecreateTaskIDs[nTask];

		// next occurrence can fail if we've run out of occurrences
		COleDateTime dtNext;
		BOOL bDueDate = TRUE;

		if (!m_data.GetTaskNextOccurrence(dwTaskID, dtNext, bDueDate))
			continue;

		CTaskFile task;
		PrepareTaskfileForTasks(task, TDCGT_ALL);
		
		VERIFY(m_exporter.ExportTask(dwTaskID, task, NULL, FALSE));

		DWORD dwNewTaskID = RecreateRecurringTaskInTree(task, dtNext, bDueDate);

		if (dwNewTaskID)
		{
			// Save off taskIDs for the end
			aTaskIDs.Add(dwTaskID);
			aNewTaskIDs.Add(dwNewTaskID);
		}
	}

	// mark as changed
	if (aNewTaskIDs.GetSize())
	{
		SelectTasks(aNewTaskIDs);
		SetModified(TDCA_NEWTASK, aNewTaskIDs);

		// notify parent of all new tasks
		int nTask = aTaskIDs.GetSize();

		while (nTask--)
			GetParent()->SendMessage(WM_TDCN_RECREATERECURRINGTASK, aTaskIDs[nTask], aNewTaskIDs[nTask]);
	}

	// always
	m_aRecreateTaskIDs.RemoveAll();

	return 0L;
}

DWORD CToDoCtrl::RecreateRecurringTaskInTree(const CTaskFile& task, const COleDateTime& dtNext, BOOL bDueDate)
{
	DWORD dwTaskID = task.GetTaskID(task.GetFirstTask()); // existing task ID

	// insert below existing
	HTREEITEM hti = m_taskTree.GetItem(dwTaskID);
	ASSERT(hti);

	HTREEITEM htiParent = m_taskTree.GetParentItem(hti);
	HTREEITEM htiNew = PasteTaskToTree(task, task.GetFirstTask(), htiParent, hti, TDCR_YES, TRUE);

	DWORD dwNewTaskID = GetTaskID(htiNew);
	InitialiseNewRecurringTask(dwTaskID, dwNewTaskID, dtNext, bDueDate);

	return dwNewTaskID;
}

void CToDoCtrl::InitialiseNewRecurringTask(DWORD dwPrevTaskID, DWORD dwNewTaskID, const COleDateTime& dtNext, BOOL bDueDate)
{
	// reset new task(s) state to 'undone' including all children
	SetTaskDone(dwNewTaskID, 0.0, TRUE, TRUE, FALSE);

	// we need to move both the due date and the start date forward
	AdjustNewRecurringTasksDates(dwPrevTaskID, dwNewTaskID, dtNext, bDueDate);

	// Clear certain attributes
	m_data.ClearTaskAttribute(dwNewTaskID, TDCA_TIMESPENT, TRUE); 
	m_data.ClearTaskAttribute(dwNewTaskID, TDCA_PERCENT, TRUE);

	// Set some defaults
	m_data.SetTaskStatus(dwNewTaskID, m_tdiDefault.sStatus);

	// Reset number of occurrences
	m_data.ResetRecurringSubtaskOccurrences(dwNewTaskID);

	// Special handling for recreated tasks
	if (dwNewTaskID != dwPrevTaskID)
	{
		// the task ID has effectively changed so fix up those
		// tasks that previously had a dependency
		m_data.FixupTaskLocalDependentsIDs(dwNewTaskID, dwPrevTaskID);

		// Restore previous comments format
		CONTENTFORMAT cfComments;
		const CBinaryData& customComments = m_data.GetTaskCustomComments(dwPrevTaskID, cfComments);

		m_data.SetTaskCommentsType(dwNewTaskID, cfComments);
	}

	// optionally clear the comments
	TDCRECURRENCE tr;

	if (!m_data.GetTaskRecurrence(dwNewTaskID, tr) || !tr.bPreserveComments)
	{
		m_data.ClearTaskAttribute(dwNewTaskID, TDCA_COMMENTS, TRUE);

		m_sTextComments.Empty();
		m_customComments.Empty();

		UpdateComments(FALSE);
	}
}

TDC_SET CToDoCtrl::SetTaskDone(DWORD dwTaskID, const COleDateTime& date, 
								BOOL bAndSubtasks, BOOL bUpdateAllSubtaskDates, BOOL bIsSubtask)
{
	ASSERT(bAndSubtasks || !bIsSubtask);
	ASSERT(!CDateHelper::IsDateSet(date) || !bUpdateAllSubtaskDates);

	TDC_SET nRes = SET_NOCHANGE;

	// If bUpdateAllSubtaskDates == FALSE, we only update a subtask's 
	// completion date if its completion state has also changed
	BOOL bDone = CDateHelper::IsDateSet(date);
	BOOL bWasDone = m_data.IsTaskDone(dwTaskID);
	BOOL bStateChange = ((bDone && !bWasDone) || (!bDone && bWasDone));
	BOOL bDateChange = bStateChange;
	
	if (!bDateChange && bDone && bWasDone)
		bDateChange = (date != m_data.GetTaskDate(dwTaskID, TDCD_DONE));
	
	if (bDateChange && (!bIsSubtask || bUpdateAllSubtaskDates || bStateChange))
	{
		if (m_data.SetTaskDate(dwTaskID, TDCD_DONE, date) == SET_CHANGE)
		{
			nRes = SET_CHANGE;

			// update 'status' if done status has switched
			if (bStateChange && !m_sCompletionStatus.IsEmpty())
			{
				if (bDone)
				{
					m_data.SetTaskStatus(dwTaskID, m_sCompletionStatus);
				}
				else
				{
					m_data.SetTaskStatus(dwTaskID, _T(""));
				}
			}
		}
	}
		
	if (m_timeTracking.IsTrackingTask(dwTaskID, FALSE))
	{
		EndTimeTracking(TRUE, TRUE);
		UpdateControls(FALSE); // don't update comments
	}

	if (bAndSubtasks && m_data.TaskHasSubtasks(dwTaskID))
	{
		const TODOSTRUCTURE* pTDS = m_data.LocateTask(dwTaskID);
		ASSERT(pTDS);

		for (int nSubtask = 0; nSubtask < pTDS->GetSubTaskCount(); nSubtask++)
		{
			DWORD dwSubtaskID = pTDS->GetSubTaskID(nSubtask);

			if (SetTaskDone(dwSubtaskID, date, TRUE, bUpdateAllSubtaskDates, TRUE) == SET_CHANGE)
				nRes = SET_CHANGE;
		}
	}

	return nRes;
}

BOOL CToDoCtrl::SetSelectedTaskPercentDone(int nPercent, BOOL bOffset)
{
	return SetSelectedTaskPercentDone(nPercent, bOffset, CDateHelper::NullDate());
}

BOOL CToDoCtrl::CanSetSelectedTaskPercentDone(BOOL bToToday) const
{
	if (!CanEditSelectedTask(TDCA_PERCENT))
		return FALSE;

	// Extra date processing
	if (bToToday)
	{
		if (HasStyle(TDCS_AUTOCALCPERCENTDONE))
			return FALSE;

		if (SelectedTasksHaveChildren() && // ie. some are parents
			HasStyle(TDCS_AVERAGEPERCENTSUBCOMPLETION))
		{
			return FALSE;
		}
	}

	return TRUE;
}

BOOL CToDoCtrl::CanSetSelectedTaskPercentDoneToToday() const
{
	return CanSetSelectedTaskPercentDone(TRUE);
}

BOOL CToDoCtrl::SetSelectedTaskPercentDoneToToday()
{
	return SetSelectedTaskPercentDone(-1, FALSE, CDateHelper::GetEndOfDay(COleDateTime::GetCurrentTime()));
}

// internal helper
BOOL CToDoCtrl::SetSelectedTaskPercentDone(int nPercent, BOOL bOffset, const COleDateTime& date)
{
	// Sanity check
	BOOL bDateIsvalid = CDateHelper::IsDateSet(date);

	ASSERT((bDateIsvalid && (nPercent == -1) && !bOffset) || 
			(!bDateIsvalid && (bOffset || (nPercent >= 0))));

	if (!CanSetSelectedTaskPercentDone(bDateIsvalid))
		return FALSE;

	Flush();

	IMPLEMENT_DATA_UNDO_EDIT(m_data);

	CDWordArray aModTaskIDs;
	POSITION pos = TSH().GetFirstItemPos();
	BOOL bDoneChange = FALSE;

	// Keep track of what we've processed to avoid offsetting
	// the same task multiple times via references
	CDWordSet mapProcessed;

	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);

		if (bOffset && mapProcessed.Has(dwTaskID))
			continue;

		if (bDateIsvalid)
		{
			COleDateTimeRange dtStartDue;

			if (!m_data.GetTaskStartDueDates(dwTaskID, dtStartDue))
				continue;

			nPercent = (int)(dtStartDue.CalcProportion(date) * 100);
			ASSERT(!bOffset);
		}

		BOOL bWasDone = m_data.IsTaskDone(dwTaskID);

		if (!HandleModResult(dwTaskID, m_data.SetTaskPercent(dwTaskID, nPercent, bOffset), aModTaskIDs))
			return FALSE;

		if (bOffset)
			mapProcessed.Add(dwTaskID);

		bDoneChange |= (bWasDone != m_data.IsTaskDone(dwTaskID));
	}

	if (aModTaskIDs.GetSize())
	{
		int nPercent = GetSelectedTaskPercent();

		// don't update m_nPercentDone for multiple selection
		// else they all end up as the same value
		if ((nPercent != -1) && (m_nPercentDone != nPercent))
		{
			m_nPercentDone = nPercent;
			UpdateDataEx(this, IDC_PERCENT, m_nPercentDone, FALSE);
		}

		CTDCAttributeMap mapAttrib(TDCA_PERCENT);

		if (bDoneChange)
			mapAttrib.Add(TDCA_DONEDATE);

		SetModified(mapAttrib, aModTaskIDs, TRUE);
	}

	return TRUE;
}

BOOL CToDoCtrl::SetSelectedTaskCost(const TDCCOST& cost, BOOL bOffset)
{
	if (!CanEditSelectedTask(TDCA_COST))
		return FALSE;

	Flush();
	
	IMPLEMENT_DATA_UNDO_EDIT(m_data);
		
	CDWordArray aModTaskIDs;
	POSITION pos = TSH().GetFirstItemPos();
	
	// Keep track of what we've processed to avoid offsetting
	// the same task multiple times via references
	CDWordSet mapProcessed;

	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);

		if (bOffset && mapProcessed.Has(dwTaskID))
			continue;

		if (!HandleModResult(dwTaskID, m_data.SetTaskCost(dwTaskID, cost, bOffset), aModTaskIDs))
			return FALSE;

		if (bOffset)
			mapProcessed.Add(dwTaskID);
	}

	if (aModTaskIDs.GetSize())
	{
		TDCCOST costSel;
		
		if (GetSelectedTaskCost(costSel) && (m_cost != costSel))
		{
			m_cost = costSel;
			CTDCDialogHelper().UpdateDataEx(this, IDC_COST, m_cost, FALSE, DECIMALS);
		}

		SetModified(TDCA_COST, aModTaskIDs);
	}

	return TRUE;
}

BOOL CToDoCtrl::SetSelectedTaskRecurrence(const TDCRECURRENCE& tr)
{
	if (!CanEditSelectedTask(TDCA_RECURRENCE))
		return FALSE;

	Flush();
	
	IMPLEMENT_DATA_UNDO_EDIT(m_data);

	CDWordArray aModTaskIDs;
	POSITION pos = TSH().GetFirstItemPos();
	
	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);

		if (!HandleModResult(dwTaskID, m_data.SetTaskRecurrence(dwTaskID, tr), aModTaskIDs))
			return FALSE;
	}
	
	if (aModTaskIDs.GetSize())
	{
		if (m_tRecurrence != tr)
		{
			m_tRecurrence = tr;
			m_eRecurrence.SetRecurrenceOptions(tr);
		}

		// cache the last top-level options
		if (tr.IsRecurring())
		{
			m_nDefRecurFrom = tr.nRecalcFrom;
			m_nDefRecurReuse = tr.nReuse;
		}
		
		SetModified(TDCA_RECURRENCE, aModTaskIDs);
	}
	
	return TRUE;
}

void CToDoCtrl::SetPercentDoneIncrement(int nAmount)
{
	m_nPercentIncrement = abs(nAmount);
	m_nPercentIncrement = max(1, min(50, m_nPercentIncrement));

	if (m_spinPercent.GetSafeHwnd())
	{
		UDACCEL uda = { 0, m_nPercentIncrement };
		m_spinPercent.SetAccel(1, &uda);
	}
}

BOOL CToDoCtrl::IncrementSelectedTaskPercentDone(BOOL bUp)
{
	return SetSelectedTaskPercentDone((bUp ? m_nPercentIncrement : -m_nPercentIncrement), TRUE);
}

BOOL CToDoCtrl::SetSelectedTaskTimeEstimate(const TDCTIMEPERIOD& timeEst, BOOL bOffset)
{
	if (!CanEditSelectedTask(TDCA_TIMEEST))
		return FALSE;

	Flush();
	
	POSITION pos = TSH().GetFirstItemPos();
	CDWordArray aModTaskIDs;
	
	IMPLEMENT_DATA_UNDO_EDIT(m_data);
		
	// Keep track of what we've processed to avoid offsetting
	// the same task multiple times via references
	CDWordSet mapProcessed;

	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);

		if (bOffset && mapProcessed.Has(dwTaskID))
			continue;

		// ignore parent tasks
		if (m_data.IsTaskParent(dwTaskID) && !HasStyle(TDCS_ALLOWPARENTTIMETRACKING))
			continue;

		if (m_data.SetTaskTimeEstimate(dwTaskID, timeEst, bOffset) == SET_CHANGE)
			aModTaskIDs.Add(dwTaskID);

		if (bOffset)
			mapProcessed.Add(dwTaskID);
	}
	
	if (aModTaskIDs.GetSize())
	{
		// Update the time estimate field
		TDCTIMEPERIOD time;

		if (GetSelectedTaskTimeEstimate(time) && (m_timeEstimate != time))
		{
			// note: setting the time field changes m_timeEstimate.nUnits
			// so we have to do them separately
			m_timeEstimate = time;
			CTDCDialogHelper().UpdateDataEx(this, m_eTimeEstimate, m_timeEstimate, FALSE, DECIMALS);
		}

		// Recalc other attributes if only one item selected
		if (GetSelectedCount() == 1)
		{
			// update % complete?
			if (HasStyle(TDCS_AUTOCALCPERCENTDONE))
			{
				m_nPercentDone = m_calculator.GetTaskPercentDone(GetSelectedTaskID());
				UpdateDataEx(this, IDC_PERCENT, m_nPercentDone, FALSE);
			}

			// update start/due date?
			if (HasStyle(TDCS_SYNCTIMEESTIMATESANDDATES))
			{
				COleDateTime dtDue = GetSelectedTaskDate(TDCD_DUE);
				SetCtrlDate(m_dtcDue, dtDue, dtDue);

				COleDateTime dtStart = GetSelectedTaskDate(TDCD_START);
				SetCtrlDate(m_dtcStart, dtStart, dtStart);
			}
		}

		CTDCAttributeMap mapAttribIDs(TDCA_TIMEEST);

		// may also need to report percent and/or date changes
		if (HasStyle(TDCS_AUTOCALCPERCENTDONE))
			mapAttribIDs.Add(TDCA_PERCENT);

		if (HasStyle(TDCS_SYNCTIMEESTIMATESANDDATES))
		{
			mapAttribIDs.Add(TDCA_STARTDATE);
			mapAttribIDs.Add(TDCA_DUEDATE);
		}

		SetModified(mapAttribIDs, aModTaskIDs, TRUE);
	}

	return TRUE;
}

BOOL CToDoCtrl::SetSelectedTaskTimeSpent(const TDCTIMEPERIOD& timeSpent, BOOL bOffset)
{
	if (!CanEditSelectedTask(TDCA_TIMESPENT))
		return FALSE;

	Flush();
	
	POSITION pos = TSH().GetFirstItemPos();
	CDWordArray aModTaskIDs;
	
	IMPLEMENT_DATA_UNDO_EDIT(m_data);
		
	// Keep track of what we've processed to avoid offsetting
	// the same task multiple times via references
	CDWordSet mapProcessed;

	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);

		if (bOffset && mapProcessed.Has(dwTaskID))
			continue;

		// ignore parent tasks
		if (m_data.IsTaskParent(dwTaskID) && !HasStyle(TDCS_ALLOWPARENTTIMETRACKING))
			continue;

		if (m_data.SetTaskTimeSpent(dwTaskID, timeSpent, bOffset) == SET_CHANGE)
			aModTaskIDs.Add(dwTaskID);

		if (bOffset)
			mapProcessed.Add(dwTaskID);
	}
	
	if (aModTaskIDs.GetSize())
	{
		// Update the time spent field
		TDCTIMEPERIOD time;

		if (GetSelectedTaskTimeSpent(time) && (m_timeSpent != time))
		{
			// note: setting the time field changes m_timeSpent.nUnits
			// so we have to do them separately
			m_timeSpent = time;
			CTDCDialogHelper().UpdateDataEx(this, m_eTimeSpent, m_timeSpent, FALSE, DECIMALS);
		}
		
		// update % complete?
		if (HasStyle(TDCS_AUTOCALCPERCENTDONE) && (GetSelectedCount() == 1))
		{
			m_nPercentDone = m_calculator.GetTaskPercentDone(GetSelectedTaskID());		
			UpdateDataEx(this, IDC_PERCENT, m_nPercentDone, FALSE);
		}
		
		SetModified(TDCA_TIMESPENT, aModTaskIDs);
	}
	
	return TRUE;
}

BOOL CToDoCtrl::SetSelectedTaskTimeEstimateUnits(TDC_UNITS nUnits, BOOL bRecalcTime)
{
	if (!CanEditSelectedTask(TDCA_TIMEEST))
		return FALSE;

	Flush();
	
	POSITION pos = TSH().GetFirstItemPos();
	CDWordArray aModTaskIDs;
	
	IMPLEMENT_DATA_UNDO_EDIT(m_data);
		
	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);

		// ignore parent tasks
		if (m_data.IsTaskParent(dwTaskID) && !HasStyle(TDCS_ALLOWPARENTTIMETRACKING))
			continue;

		TDCTIMEPERIOD timeEst;
		m_data.GetTaskTimeEstimate(dwTaskID, timeEst);

		if (timeEst.SetUnits(nUnits, bRecalcTime))
		{
			if (!HandleModResult(dwTaskID, m_data.SetTaskTimeEstimate(dwTaskID, timeEst), aModTaskIDs))
				return FALSE;
		}
	}
	
	if (aModTaskIDs.GetSize())
	{
		if (m_timeEstimate.nUnits != nUnits)
		{
			m_timeEstimate.nUnits = nUnits;
			m_eTimeEstimate.SetUnits(m_timeEstimate.GetTHUnits());
		}

		// update other controls if only one item selected
		if (GetSelectedCount() == 1)
		{
			if (bRecalcTime)
			{
				VERIFY(GetSelectedTaskTimeEstimate(m_timeEstimate));
				CTDCDialogHelper().UpdateDataEx(this, m_eTimeEstimate, m_timeEstimate, FALSE, DECIMALS);
			}
			// update % complete?
			else if (HasStyle(TDCS_AUTOCALCPERCENTDONE))
			{
				m_nPercentDone = m_calculator.GetTaskPercentDone(GetSelectedTaskID());		
				UpdateDataEx(this, IDC_PERCENT, m_nPercentDone, FALSE);
			}

			// update due date?
			if (HasStyle(TDCS_SYNCTIMEESTIMATESANDDATES))
			{
				COleDateTime dtDue = GetSelectedTaskDate(TDCD_DUE);
				SetCtrlDate(m_dtcDue, dtDue, dtDue);
			}
		}
			
		SetModified(TDCA_TIMEEST, aModTaskIDs);
	}
	
	return TRUE;
}

BOOL CToDoCtrl::SetSelectedTaskTimeSpentUnits(TDC_UNITS nUnits, BOOL bRecalcTime)
{
	if (!CanEditSelectedTask(TDCA_TIMESPENT))
		return FALSE;

	Flush();
	
	POSITION pos = TSH().GetFirstItemPos();
	CDWordArray aModTaskIDs;
	
	IMPLEMENT_DATA_UNDO_EDIT(m_data);

	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);

		// ignore parent tasks
		if (m_data.IsTaskParent(dwTaskID) && !HasStyle(TDCS_ALLOWPARENTTIMETRACKING))
			continue;

		TDCTIMEPERIOD timeSpent;
		m_data.GetTaskTimeSpent(dwTaskID, timeSpent);

		if (timeSpent.SetUnits(nUnits, bRecalcTime))
		{
			if (!HandleModResult(dwTaskID, m_data.SetTaskTimeSpent(dwTaskID, timeSpent), aModTaskIDs))
				return FALSE;
		}
	}
	
	// update UI
	if (aModTaskIDs.GetSize())
	{
		if (m_timeSpent.nUnits != nUnits)
		{
			m_timeSpent.nUnits = nUnits;
			m_eTimeSpent.SetUnits(m_timeSpent.GetTHUnits());
		}

		// update controls if only one item selected
		if (GetSelectedCount() == 1)
		{
			if (bRecalcTime)
			{
				VERIFY(GetSelectedTaskTimeSpent(m_timeSpent));
				CTDCDialogHelper().UpdateDataEx(this, m_eTimeSpent, m_timeSpent, FALSE, DECIMALS);
			}
			// update % complete?
			else if (HasStyle(TDCS_AUTOCALCPERCENTDONE))
			{
				m_nPercentDone = m_calculator.GetTaskPercentDone(GetSelectedTaskID());		
				UpdateDataEx(this, IDC_PERCENT, m_nPercentDone, FALSE);
			}
		}
		
		SetModified(TDCA_TIMESPENT, aModTaskIDs);
	}
	
	return TRUE;
}

BOOL CToDoCtrl::HandleModResult(DWORD dwTaskID, TDC_SET nRes, CDWordArray& aModTaskIDs)
{
	switch (nRes)
	{
	case SET_FAILED:
		return FALSE;

	case SET_CHANGE:
		aModTaskIDs.Add(dwTaskID);
		break;
	}

	return TRUE;
}

BOOL CToDoCtrl::SetTextChange(TDC_ATTRIBUTE nAttrib, CString& sItem, const CString& sNewItem, 
							  UINT nIDC, const CDWordArray& aModTaskIDs, CAutoComboBox* pCombo)
{
	ASSERT(CanEditSelectedTask(nAttrib));
	
	if (aModTaskIDs.GetSize())
	{
		if (sItem != sNewItem)
		{
			// make sure it exists
			if (pCombo)
				pCombo->AddUniqueItem(sNewItem);

			sItem = sNewItem;
			UpdateDataEx(this, nIDC, sItem, FALSE);
		}
		
		SetModified(nAttrib, aModTaskIDs);
	}
	
	return TRUE;
}

BOOL CToDoCtrl::SetSelectedTaskAllocTo(const CStringArray& aAllocTo, BOOL bAppend)
{
	return SetSelectedTaskArray(TDCA_ALLOCTO, aAllocTo, bAppend, m_cbAllocTo);
}

BOOL CToDoCtrl::SetSelectedTaskAllocBy(const CString& sAllocBy)
{
	if (!CanEditSelectedTask(TDCA_ALLOCBY))
		return FALSE;

	Flush();
	
	IMPLEMENT_DATA_UNDO_EDIT(m_data);

	CDWordArray aModTaskIDs;
	POSITION pos = TSH().GetFirstItemPos();
	
	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);

		if (!HandleModResult(dwTaskID, m_data.SetTaskAllocBy(dwTaskID, sAllocBy), aModTaskIDs))
			return FALSE;
	}
	
	return SetTextChange(TDCA_ALLOCBY, m_sAllocBy, sAllocBy, IDC_ALLOCBY, aModTaskIDs, &m_cbAllocBy);
}

BOOL CToDoCtrl::SetSelectedTaskVersion(const CString& sVersion)
{
	if (!CanEditSelectedTask(TDCA_VERSION))
		return FALSE;

	Flush();
	
	IMPLEMENT_DATA_UNDO_EDIT(m_data);
		
	CDWordArray aModTaskIDs;
	POSITION pos = TSH().GetFirstItemPos();

	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);
		
		if (!HandleModResult(dwTaskID, m_data.SetTaskVersion(dwTaskID, sVersion), aModTaskIDs))
			return FALSE;
	}
	
	return SetTextChange(TDCA_VERSION, m_sVersion, sVersion, IDC_VERSION, aModTaskIDs, &m_cbVersion);
}

BOOL CToDoCtrl::SetSelectedTaskStatus(const CString& sStatus)
{
	if (!CanEditSelectedTask(TDCA_STATUS))
		return FALSE;

	Flush();
	
	IMPLEMENT_DATA_UNDO_EDIT(m_data);
		
	CDWordArray aModTaskIDs;
	POSITION pos = TSH().GetFirstItemPos();
	
	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);
		
		if (!HandleModResult(dwTaskID, m_data.SetTaskStatus(dwTaskID, sStatus),	aModTaskIDs))
			return FALSE;
	}

	if (!SetTextChange(TDCA_STATUS, m_sStatus, sStatus, IDC_STATUS, aModTaskIDs, &m_cbStatus))
		return FALSE;

	if (!m_sCompletionStatus.IsEmpty() && HasStyle(TDCS_SYNCCOMPLETIONTOSTATUS))
		SetSelectedTaskDone(sStatus == m_sCompletionStatus);

	return TRUE;
}

BOOL CToDoCtrl::SetSelectedTaskArray(TDC_ATTRIBUTE nAttrib, const CStringArray& aItems, 
									BOOL bAppend, CCheckComboBox& combo)
{
	CDWordArray aModTaskIDs;

	if (SET_FAILED == SetSelectedTaskArray(nAttrib, aItems, bAppend, aModTaskIDs))
		return FALSE;
	
	if (aModTaskIDs.GetSize())
	{
		// Update combo to show new selection
		ASSERT(aModTaskIDs.GetSize());

		switch (nAttrib)
		{
		case TDCA_CATEGORY:	
		case TDCA_TAGS:			
		case TDCA_ALLOCTO:		
			if (bAppend)
			{
				CStringArray aTaskItems;
				m_data.GetTaskArray(aModTaskIDs[0], nAttrib, aTaskItems);

				combo.SetChecked(aTaskItems);
			}
			else
			{
				combo.SetChecked(aItems);
			}
			break;

		default:
			ASSERT(0);
			return FALSE;
		}
	}

	return TRUE;
}

TDC_SET CToDoCtrl::SetSelectedTaskArray(TDC_ATTRIBUTE nAttrib, const CStringArray& aItems, 
										BOOL bAppend, CDWordArray& aModTaskIDs)
{
	if (!CanEditSelectedTask(nAttrib))
		return SET_FAILED;

	Flush();

	IMPLEMENT_DATA_UNDO_EDIT(m_data);

	aModTaskIDs.RemoveAll();
	POSITION pos = TSH().GetFirstItemPos();

	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);
		
		if (!HandleModResult(dwTaskID, m_data.SetTaskArray(dwTaskID, nAttrib, aItems, bAppend), aModTaskIDs))
			return SET_FAILED;
	}

	if (!aModTaskIDs.GetSize())
		return SET_NOCHANGE;

	SetModified(nAttrib, aModTaskIDs);
	return SET_CHANGE;
}

BOOL CToDoCtrl::SetSelectedTaskArray(TDC_ATTRIBUTE nAttrib, const CCheckComboBox& combo)
{
	CStringArray aChecked, aUnchecked, aTaskItems;
	BOOL bMergeItems = FALSE;

	combo.GetChecked(aChecked, CCBC_CHECKED);
		
	if (combo.IsAnyChecked(CCBC_MIXED))
	{
		combo.GetChecked(aUnchecked, CCBC_UNCHECKED);
		bMergeItems = TRUE;
	}
	
	POSITION pos = TSH().GetFirstItemPos();
	CDWordArray aModTaskIDs;

	IMPLEMENT_DATA_UNDO_EDIT(m_data);

	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);

		// We only need to be careful if the combo has any mixed items
		// and if the task itself has any current array items
		if (bMergeItems && m_data.GetTaskArray(dwTaskID, nAttrib, aTaskItems))
		{
			Misc::AddUniqueItems(aChecked, aTaskItems);
			Misc::RemoveItems(aUnchecked, aTaskItems);
		}
		else
		{
			aTaskItems.Copy(aChecked);
		}
		
		if (!HandleModResult(dwTaskID, m_data.SetTaskArray(dwTaskID, nAttrib, aTaskItems, FALSE), aModTaskIDs))
			return FALSE;
	}

	if (aModTaskIDs.GetSize())
		SetModified(nAttrib, aModTaskIDs);

	return TRUE;
}

BOOL CToDoCtrl::SetSelectedTaskCategories(const CStringArray& aCats, BOOL bAppend)
{
	return SetSelectedTaskArray(TDCA_CATEGORY, aCats, bAppend, m_cbCategory);
}

BOOL CToDoCtrl::SetSelectedTaskTags(const CStringArray& aTags, BOOL bAppend)
{
	return SetSelectedTaskArray(TDCA_TAGS, aTags, bAppend, m_cbTags);
}

BOOL CToDoCtrl::SetSelectedTaskFileRefs(const CStringArray& aFilePaths, BOOL bAppend)
{
	return SetSelectedTaskFileRefs(aFilePaths, bAppend, FALSE);
}

BOOL CToDoCtrl::SetSelectedTaskFileRefs(const CStringArray& aFilePaths, BOOL bAppend, BOOL bCtrlEdited)
{
	CStringArray aFileRefs;

	if (bAppend)
		GetSelectedTaskFileRefs(aFileRefs); // full paths

	Misc::AddUniqueItems(aFilePaths, aFileRefs);

	// convert to relative paths
	MakeRelativePaths(aFileRefs);

	CDWordArray aModTaskIDs;

	if (SET_FAILED == SetSelectedTaskArray(TDCA_FILEREF, aFileRefs, bAppend, aModTaskIDs))
		return FALSE;
	
	if (aModTaskIDs.GetSize())
	{
		m_aFileRefs.Copy(aFileRefs);

		if (!bCtrlEdited)
			m_cbFileRef.SetFileList(aFileRefs);
	}

	return TRUE;
}

BOOL CToDoCtrl::SetSelectedTaskDependencies(const CStringArray& aDepends, BOOL bAppend)
{
	return SetSelectedTaskDependencies(aDepends, bAppend, FALSE);
}

BOOL CToDoCtrl::SetSelectedTaskDependencies(const CStringArray& aDepends, BOOL bAppend, BOOL bEdit)
{
	CDWordArray aModTaskIDs;
	
	if (SET_FAILED == SetSelectedTaskArray(TDCA_DEPENDENCY, aDepends, bAppend, aModTaskIDs))
		return FALSE;
	
	if (aModTaskIDs.GetSize())
	{
		// Start and due dates might also have changed
		if (HasStyle(TDCS_AUTOADJUSTDEPENDENCYDATES))
		{
			UpdateDateTimeControls(TRUE);
		}

		// We only update the control if not editing otherwise
		// if the user is partially way thru typing a task ID
		// and the partial ID does not exist then it gets 
		// removed from the edit field. 
		if (!bEdit)
		{
			m_sDepends = Misc::FormatArray(aDepends);
			UpdateDataEx(this, IDC_DEPENDS, m_sDepends, FALSE);
		}
	}

	return TRUE;
}

void CToDoCtrl::PauseTimeTracking(BOOL bPause) 
{ 
	if (m_timeTracking.PauseTracking(bPause))
	{
		if (bPause)
			KillTimer(TIMER_TRACK);
		else
			SetTimer(TIMER_TRACK, TIMETRACKPERIOD, NULL);
	}
}

CString CToDoCtrl::FormatTimeTrackingElapsedTime() const
{
	return m_timeTracking.FormatElapsedTime();
}

void CToDoCtrl::ResetTimeTrackingElapsedMinutes()
{
	m_timeTracking.ResetReminderIsDue();
}

BOOL CToDoCtrl::TimeTrackSelectedTask()
{
	if (!CanTimeTrackSelectedTask())
		return FALSE;
	
	ToggleTimeTracking(GetSelectedItem());
	
	return FALSE;
}

BOOL CToDoCtrl::CanTimeTrackSelectedTask() const
{
	if (!CanEditSelectedTask(TDCA_TIMESPENT) || (GetSelectedCount() != 1))
		return FALSE;

	DWORD dwTaskID = GetSelectedTaskID();
	
	if (dwTaskID)
		return m_timeTracking.CanTrackTask(dwTaskID);

	// else
	return FALSE;
}

BOOL CToDoCtrl::IsSelectedTaskBeingTimeTracked() const
{
	return m_timeTracking.IsTrackingSelectedTask();
}

DWORD CToDoCtrl::GetTimeTrackTaskID(BOOL bActive) const
{
	return m_timeTracking.GetTrackedTaskID(bActive);
}

BOOL CToDoCtrl::IsActivelyTimeTracking() const
{
	DWORD dwTrackedTaskID = m_timeTracking.GetTrackedTaskID(TRUE);

	return (dwTrackedTaskID != 0);
}

BOOL CToDoCtrl::SetSelectedTaskExternalID(const CString& sID)
{
	if (!CanEditSelectedTask(TDCA_EXTERNALID))
		return FALSE;

	Flush();
	
	IMPLEMENT_DATA_UNDO_EDIT(m_data);
		
	CDWordArray aModTaskIDs;
	POSITION pos = TSH().GetFirstItemPos();
	
	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);

		if (!HandleModResult(dwTaskID, m_data.SetTaskExternalID(dwTaskID, sID), aModTaskIDs))
			return FALSE;
	}
	
	return SetTextChange(TDCA_EXTERNALID, m_sExternalID, sID, IDC_EXTERNALID, aModTaskIDs);
}

BOOL CToDoCtrl::GetSelectedTaskRecurrence(TDCRECURRENCE& tr) const 
{ 
	if (!m_taskTree.GetSelectedTaskRecurrence(tr))
	{
		// initialise some options if regularity == once
		ASSERT(!tr.IsRecurring());

		tr.nReuse = m_nDefRecurReuse;
		tr.nRecalcFrom = m_nDefRecurFrom;

		return FALSE;
	}
	
	return TRUE;
}

int CToDoCtrl::GetSelectedTaskFileRefs(CStringArray& aFiles) const 
{  
	// external version always returns full paths
	return GetSelectedTaskFileRefs(aFiles, TRUE);
}

int CToDoCtrl::GetSelectedTaskFileRefs(CStringArray& aFiles, BOOL bFullPath) const 
{ 
	int nNumFiles = m_taskTree.GetSelectedTaskFileRefs(aFiles); 

	if (nNumFiles && bFullPath)
		MakeFullPaths(aFiles);

	return nNumFiles;
}

BOOL CToDoCtrl::GotoSelectedTaskFileRef(int nFile)
{
	return GotoFile(GetSelectedTaskFileRef(nFile, TRUE)); // full path
}

CString CToDoCtrl::GetSelectedTaskFileRef(int nFile, BOOL bFullPath) const 
{ 
	CString sFile = m_taskTree.GetSelectedTaskFileRef(nFile);

	if (!sFile.IsEmpty() && bFullPath && !WebMisc::IsURL(sFile))
	{
		CString sParentFolder = GetLastSaveFolder();
		FileMisc::MakeFullPath(sFile, sParentFolder); 
	}

	return sFile;
}

CString CToDoCtrl::GetSelectedTaskFileRef(int nFile) const
{
	// external version always returns full paths
	return GetSelectedTaskFileRef(nFile, TRUE);
}

BOOL CToDoCtrl::CreateNewTask(const CString& sText, TDC_INSERTWHERE nWhere, BOOL bEditText, DWORD dwDependency)
{
	if (!CanCreateNewTask(nWhere, sText))
		return FALSE;
	
	Flush();

	// handle special case when tasklist is empty
	if (GetTaskCount() == 0)
		nWhere = TDC_INSERTATBOTTOM;
	
	HTREEITEM htiParent = NULL, htiAfter = NULL;

	if (m_taskTree.GetInsertLocation(nWhere, htiParent, htiAfter))
	{
		HTREEITEM htiNew = InsertNewTask(sText, htiParent, htiAfter, bEditText, dwDependency);
		ASSERT(htiNew);

		DWORD dwTaskID = GetTaskID(htiNew);
		ASSERT(dwTaskID == (m_dwNextUniqueID - 1));

		return (htiNew != NULL);
	}

	// else
	ASSERT(0);
	return FALSE;
}

BOOL CToDoCtrl::CanCreateNewTask(TDC_INSERTWHERE nInsertWhere) const
{
	if (!CanEditSelectedTask(TDCA_NEWTASK))
		return FALSE;

	int nNumSel = GetSelectedCount();

	HTREEITEM htiParent = NULL, htiAfter = NULL;

	switch (nNumSel)
	{
	case 1:
		VERIFY (m_taskTree.GetInsertLocation(nInsertWhere, htiParent, htiAfter));
		break;

	case 0:
		// parent and sibling are NULL
		break; 

	default:
		// can't handle multiple selection
		return FALSE;
	}

	switch (nInsertWhere)
	{
	case TDC_INSERTATTOP:
	case TDC_INSERTATBOTTOM:
		return TRUE;

	// Selected item cannot be NULL and parent cannot be reference
	case TDC_INSERTATTOPOFSELTASKPARENT:
	case TDC_INSERTATBOTTOMOFSELTASKPARENT:
	case TDC_INSERTAFTERSELTASK:
	case TDC_INSERTBEFORESELTASK:
	case TDC_INSERTATTOPOFSELTASK: 
	case TDC_INSERTATBOTTOMOFSELTASK:
		{
 			if (GetSelectedItem() == NULL)
 				return FALSE;
		
			// else
			return !m_data.IsTaskReference(GetTaskID(htiParent));
		}
		break;
	}

	ASSERT(0);
	return FALSE;
}

BOOL CToDoCtrl::CanCreateNewTask(TDC_INSERTWHERE nWhere, const CString& sText) const
{
	if (!CanCreateNewTask(nWhere))
		return FALSE;
	
	if (sText.IsEmpty())
		return FALSE;

	// are we an archive and should we warn user if we are
	if (m_bArchive && HasStyle(TDCS_WARNADDDELETEARCHIVE))
	{
		if (CMessageBox::AfxShow(IDS_TDC_CONFIRMADD_TITLE, IDS_TDC_WARNADDTOARCHIVE, MB_YESNO | MB_ICONQUESTION) != IDYES) 
		{
			return FALSE;
		}
	}

	return TRUE;
}

TODOITEM* CToDoCtrl::CreateNewTask(HTREEITEM htiParent)
{
	// copy over parent attributes
	BOOL bRootParent = (!htiParent || htiParent == TVI_ROOT);
	DWORD dwParentID = bRootParent ? 0 : GetTaskID(htiParent);

	return m_data.NewTask(m_tdiDefault, dwParentID);
}

HTREEITEM CToDoCtrl::InsertNewTask(const CString& sText, HTREEITEM htiParent, HTREEITEM htiAfter, 
								BOOL bEdit, DWORD dwDependency)
{
	m_dwLastAddedID = 0;
	
	if (!CanEditSelectedTask(TDCA_NEWTASK))
		return NULL;
	
	if (sText.IsEmpty())
		return NULL;
	
	IMPLEMENT_DATA_UNDO(m_data, TDCUAT_ADD);

	// create the new task item
	TODOITEM* pTDINew = CreateNewTask(htiParent);

	if (!pTDINew)
	{
		ASSERT(0);
		return NULL;
	}
	
	// and initialize
	pTDINew->sTitle = sText;

	// copy parent attribs
	DWORD dwParentID = GetTaskID(htiParent);
	DWORD dwAfterID = GetTaskID(htiAfter);

	// handle TVI_FIRST and TVI_LAST
	// Note: we don't modify the htiAfter directly because this is 
	// needed as-is in the call to CTreeCtrl::InsertItem
	if (htiAfter == TVI_FIRST)
	{
		dwAfterID = 0;
	}
	else if (htiAfter == TVI_LAST)
	{
		dwAfterID = GetTaskID(TCH().GetLastChildItem(htiParent));
	}

	// and map it
	m_data.AddTask(m_dwNextUniqueID, pTDINew, dwParentID, dwAfterID);
	
	// add corresponding tree item
	DWORD dwTaskID = m_dwNextUniqueID;

	HTREEITEM htiNew = m_taskTree.InsertItem(dwTaskID, htiParent, htiAfter);
	ASSERT(htiNew);
	
	if (htiNew)
	{
		// issues relating to non-root parent
		if (dwParentID)
		{
			// if the parent was being clocked then stop it now
			// since having children makes a task un-clockable
			if (!HasStyle(TDCS_ALLOWPARENTTIMETRACKING))
			{
				if (m_timeTracking.IsTrackingTask(dwParentID))
					EndTimeTracking(TRUE, TRUE);
			}

			// if the parent was marked as done and the new task 
			// is NOT cancellable, we mark the parent as incomplete.
			if (!bEdit && m_data.IsTaskDone(dwParentID))
				FixupParentCompletion(dwParentID);
		}
		
		m_dwNextUniqueID++;

		// Fixup dates for dependent tasks
		if (dwDependency)
		{
			CStringArray aDepends;
			aDepends.Add(Misc::Format(dwDependency));

			m_data.SetTaskDependencies(dwTaskID, aDepends, FALSE);
		}	

		CDWordArray aModTaskIDs;
		aModTaskIDs.Add(dwTaskID);

		SelectItem(htiNew);
		SetModified(TDCA_NEWTASK, aModTaskIDs); 
		
		m_taskTree.InvalidateAll();

		if (bEdit)
			EditSelectedTaskTitle(TRUE);
		else
			SetFocusToTasks();
	}
	else // cleanup
	{
		m_data.DeleteTask(m_dwNextUniqueID, FALSE); // FALSE == no undo
	}
	
	return htiNew;
}

BOOL CToDoCtrl::CanSplitSelectedTask() const 
{ 
	return (CanEditSelectedTask(TDCA_POSITION) && m_taskTree.CanSplitSelectedTask()); 
}

BOOL CToDoCtrl::SplitSelectedTask(int nNumSubtasks)
{
	if ((nNumSubtasks < 2) || !CanSplitSelectedTask())
		return FALSE;
	
	Flush();
	
	IMPLEMENT_DATA_UNDO(m_data, TDCUAT_ADD);
	HOLD_REDRAW(NULL, m_taskTree.Tree());
	
	POSITION pos = TSH().GetFirstItemPos();
	CDWordArray aNewTaskIDs;
	
	while (pos)
	{
		HTREEITEM hti = TSH().GetNextItem(pos);
				
		if (ItemHasChildren(hti))
			continue;
		
		DWORD dwTaskID = GetTaskID(hti);

		const TODOITEM* pTDI = GetTask(dwTaskID);
		ASSERT(pTDI);
		
		if (!pTDI || pTDI->IsDone())
			continue;
		
		// Calculate how to apportion time to subtasks
		BOOL bWantInheritTimeEst = m_data.WantUpdateInheritedAttibute(TDCA_TIMEEST);
		BOOL bWantInheritStartDate = m_data.WantUpdateInheritedAttibute(TDCA_STARTDATE);
		BOOL bWantInheritDueDate = m_data.WantUpdateInheritedAttibute(TDCA_DUEDATE);
		
		double dSubDuration = 0;
		
		if (!bWantInheritStartDate && !bWantInheritDueDate)
		{
			if (pTDI->HasStart() && (pTDI->dateDue > pTDI->dateStart))
			{
				double dDuration = (pTDI->dateDue.m_dt - pTDI->dateStart.m_dt);

				if (!CDateHelper::DateHasTime(pTDI->dateDue))
					dDuration++;

				dSubDuration = (dDuration / nNumSubtasks);
			}
		}

		DWORD dwPrevSiblingID = 0;
		HTREEITEM htiPrevSibling = NULL;
		
		for (int nSubtask = 0; nSubtask < nNumSubtasks; nSubtask++)
		{
			TODOITEM* pTDISub = m_data.NewTask(*pTDI); // copy parent by default
			
			// Allocate time estimate if not inherited
			if (!bWantInheritTimeEst)
				pTDISub->timeEstimate.dAmount = (pTDI->timeEstimate.dAmount / nNumSubtasks);
			
			// Allocate all time spent to first task (clear the rest)
			if (nSubtask > 0)
				pTDISub->timeSpent.dAmount = 0;

			// Allocate duration if not inherited
			if (!bWantInheritStartDate && !bWantInheritDueDate)
			{
				if (dSubDuration)
				{
					pTDISub->dateStart += (nSubtask * dSubDuration);
					pTDISub->dateDue = pTDISub->dateStart + COleDateTime(dSubDuration);

					if (!CDateHelper::DateHasTime(pTDISub->dateDue))
					{
						pTDISub->dateDue.m_dt--;
					}
					else if (!pTDI->HasDueTime())
					{
						// clear due time if parent didn't have any
						pTDISub->dateDue = CDateHelper::GetDateOnly(pTDISub->dateDue);
					}
				}
				else if (nSubtask) // not the first
				{
					pTDISub->ClearStart();
					pTDISub->ClearDue();
				}
			}
			else
			{
				if (!bWantInheritStartDate)
					pTDISub->ClearStart();

				if (!bWantInheritDueDate)
					pTDISub->ClearDue();
			}

			// map it
			DWORD dwChildID = m_dwNextUniqueID++;
			m_data.AddTask(dwChildID, pTDISub, dwTaskID, dwPrevSiblingID);
			
			// create tree item
			htiPrevSibling = m_taskTree.InsertItem(dwChildID, hti, htiPrevSibling);
			ASSERT(htiPrevSibling);
			
			dwPrevSiblingID = dwChildID;
			aNewTaskIDs.Add(dwChildID);
		}

		// clear parent time est if not inherited
		if (!bWantInheritTimeEst)
			m_data.ClearTaskAttribute(dwTaskID, TDCA_TIMEEST);

		m_data.ClearTaskAttribute(dwTaskID, TDCA_TIMESPENT);

		// show new subtasks
		m_taskTree.ExpandItem(hti, TRUE);
	}
	
	if (aNewTaskIDs.GetSize())
	{
		ASSERT(aNewTaskIDs.GetSize() > 1);
		SetModified(TDCA_NEWTASK, aNewTaskIDs);

		return TRUE;
	}
	
	return FALSE;
}

BOOL CToDoCtrl::DeleteSelectedTask()
{
	Flush();
	
	IMPLEMENT_DATA_UNDO(m_data, TDCUAT_DELETE);
	
	// else do the standard warning against deletion
	return DeleteSelectedTask(TRUE, TRUE);
}

BOOL CToDoCtrl::DeleteSelectedTask(BOOL bWarnUser, BOOL bResetSel)
{
	if (!CanEditSelectedTask(TDCA_DELETE))
		return FALSE;
	
	// check there's something to delete
	int nSelCount = GetSelectedCount();
	
	if (!nSelCount)
		return FALSE;

	// end time tracking as required
	DWORD dwTrackedTaskID = m_timeTracking.GetTrackedTaskID(FALSE);

	if (dwTrackedTaskID && TSH().HasItem(dwTrackedTaskID))
		EndTimeTracking(FALSE, TRUE);

	// cache focus
	CSaveFocus focus;
	
	if (bWarnUser && !ConfirmDeleteAllTasks(TRUE))
		return FALSE;

	// Copy selected tasks without duplicate subtasks
	CHTIList selection;
	TSH().CopySelection(selection, TRUE);
	
	// reset anchor if we're going to delete it
	if (TSH().AnchorHasSelectedParent())
		TSH().SetAnchor(NULL);
	
	// remove real tree selection first if its going to be deleted
	// but before we do, lets figure out where the selection is
	// going to go _after_ the deletion
	DWORD dwNextID = 0;

	if (bResetSel)
		dwNextID = GetNextNonSelectedTaskID();
	
	if (TSH().HasItem(m_taskTree.GetSelectedItem()))
		TCH().SelectItem(NULL);
	
	// clear selection before deleting
	TSH().RemoveAll(TRUE, FALSE);

	CDWordArray aDelTaskIDs;
	TSH().GetItemData(selection, aDelTaskIDs);

	{
		BOOL bSingleTask = (nSelCount == 1);

		CAutoFlag af(m_bDeletingTasks, TRUE);
		CHoldRedraw hr(bSingleTask ? NULL : m_taskTree.GetSafeHwnd());
		POSITION pos = selection.GetHeadPosition();

		while (pos)
		{
			HTREEITEM hti = selection.GetNext(pos);
			DWORD dwTaskID = m_taskTree.GetTaskID(hti);

			m_taskTree.DeleteItem(hti);
			m_data.DeleteTask(dwTaskID, TRUE); // TRUE == with undo
		}
		
		// Note: CToDoCtrlData ought to have already cleaned up the data
		m_taskTree.RemoveOrphanTreeItemReferences();
	}
	m_taskTree.UpdateAll();

	// Update other views before changing selection
	SetModified(TDCA_DELETE, aDelTaskIDs);
	
	// set next selection
	if (dwNextID)
		SelectTask(dwNextID, FALSE);
	else
		UpdateControls(FALSE); // don't update comments

	// restore focus
	if (!focus.RestoreFocus())
		SetFocusToTasks();

	return TRUE;
}

DWORD CToDoCtrl::GetNextNonSelectedTaskID() const
{
	HTREEITEM hti = TSH().GetFirstItem();
	HTREEITEM htiNextSel = TCH().GetNextVisibleItem(hti, FALSE);

	// don't include items we're about to delete!
	DWORD dwSelID = GetTaskID(hti);

	while (htiNextSel)
	{
		if (TSH().HasItem(htiNextSel) || 
			TSH().HasSelectedParent(htiNextSel) || 
			m_data.IsReferenceToTask(GetTaskID(htiNextSel), dwSelID))
		{
			htiNextSel = TCH().GetNextVisibleItem(htiNextSel);
		}
		else
		{
			break;
		}
	}

	// else try previous item
	if (!htiNextSel)
	{
		htiNextSel = TCH().GetPrevVisibleItem(hti, FALSE);

		// don't include items we're about to delete!
		while (htiNextSel)
		{
			if (TSH().HasItem(htiNextSel) || 
				TSH().HasSelectedParent(htiNextSel) ||
				m_data.IsReferenceToTask(GetTaskID(htiNextSel), dwSelID))
			{
				htiNextSel = TCH().GetPrevVisibleItem(htiNextSel);
			}
			else
			{
				break;
			}
		}
	}

	return GetTaskID(htiNextSel);
}

BOOL CToDoCtrl::GetLabelEditRect(CRect& rScreen)
{
	if (m_taskTree.EnsureSelectionVisible())
		return m_taskTree.GetLabelEditRect(rScreen);

	// else
	return FALSE;
}

BOOL CToDoCtrl::EditSelectedTaskTitle(BOOL bTaskIsNew)
{
	if (!CanEditSelectedTask(TDCA_TASKNAME))
		return FALSE;

	if (IsTaskLabelEditing())
		return TRUE;
	
	// get edit rect
	CRect rPos;
	
	if (!GetLabelEditRect(rPos))
		return FALSE;

	// make sure it's at least 150 px long
	rPos.right = max(rPos.right, rPos.left + 150);

	// create edit if nec.
	if (!m_eTaskName.GetSafeHwnd() && !m_eTaskName.Create(this, IDC_FTC_EDIT, WS_POPUP | WS_BORDER))
		return FALSE;

	// start
	DWORD dwSelTaskID = GetSelectedTaskID();
	ASSERT (dwSelTaskID);

	if (bTaskIsNew)
		m_dwLastAddedID = dwSelTaskID;
	
	// save task id being edited
	SetEditTitleTaskID(dwSelTaskID);

	// set font
	CFont* pFontTree = m_taskTree.Tree().GetFont();

	if (pFontTree)
		m_eTaskName.SetFont(pFontTree);
	
	// set text
	m_eTaskName.SetWindowText(GetSelectedTaskTitle());
	
	// zero the left margin so that the text position remains the same
	m_eTaskName.SetMargins(0, HIWORD(m_eTaskName.GetMargins()));
	
	// move to the top of the Z-order
	m_eTaskName.SetWindowPos(&wndTopMost, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

	// and show
	m_eTaskName.Show(rPos);

	return TRUE;
}

BOOL CToDoCtrl::IsTaskLabelEditing() const
{ 
	return (CanEditSelectedTask(TDCA_TASKNAME) && (m_dwEditTitleTaskID != 0));
}

LRESULT CToDoCtrl::OnLabelEditEnd(WPARAM /*wParam*/, LPARAM lParam)
{
	ASSERT(m_dwEditTitleTaskID);

	CLockUpdates lu(m_taskTree);

	CString sText;
	m_eTaskName.GetWindowText(sText);

	if (GetSelectedCount() == 0) // user clicked into space
		SelectTask(m_dwEditTitleTaskID);

	// end can occurs either when the user selected return
	// or if the edit box loses the focus so we need to check
	// the lParam and only set the focus if the user chose return
	if (lParam)
		SetFocusToTasks();

	if (!sText.IsEmpty())
	{
		Flush();

		// Special handling: For new tasks we extend the previous undo
		BOOL bNewTask = (m_dwEditTitleTaskID == m_dwLastAddedID);
		TDC_UNDOACTIONTYPE nAction = (bNewTask ? TDCUAT_ADD : TDCUAT_EDIT);

		IMPLEMENT_DATA_UNDO_EXTEND(m_data, nAction, bNewTask);
		
		int nRes = m_data.SetTaskTitle(m_dwEditTitleTaskID, sText);
		
		if ((nRes == SET_CHANGE) || bNewTask)
		{
			CDWordArray aModTaskIDs;
			aModTaskIDs.Add(m_dwEditTitleTaskID);

			m_taskTree.InvalidateSelection(TRUE);
			SetModified(TDCA_TASKNAME, aModTaskIDs);

			if (bNewTask)
			{
				// If this was a new task and the parent was marked as done, 
				// now mark it as incomplete
				DWORD dwParentID = m_data.GetTaskParentID(m_dwEditTitleTaskID);
				
				if (dwParentID && m_data.IsTaskDone(dwParentID))
					FixupParentCompletion(dwParentID);

				// For new tasks we did not sort so we may need to do so
				// now if sorting by anything other than 'title' because 
				// will already have been handled by SetModified
				if (m_taskTree.IsMultiSorting() || (m_taskTree.IsSorting() && !m_taskTree.IsSortingBy(TDCC_CLIENT)))
					m_taskTree.Resort();
			}
		}
	}

	SetEditTitleTaskID(0);

	return 0L;
}

void CToDoCtrl::SetEditTitleTaskID(DWORD dwTaskID)
{	
	m_dwEditTitleTaskID = dwTaskID;
	m_taskTree.SetEditTitleTaskID(dwTaskID);

	if (dwTaskID == 0)
		m_dwLastAddedID = 0;
}

LRESULT CToDoCtrl::OnLabelEditCancel(WPARAM /*wParam*/, LPARAM lParam)
{
	if (GetSelectedCount() == 0) // user clicked into space
	{
		SelectTask(m_dwEditTitleTaskID);
	}
	// user hit escape key so we may need to delete this task if newly created
	else if (GetSelectedTaskID() == m_dwLastAddedID) 
	{
		ASSERT (lParam);
		UNREFERENCED_PARAMETER(lParam);

		// make sure this item is not selected
		HTREEITEM hti = GetSelectedItem();
		ASSERT(GetTaskID(hti) == m_dwLastAddedID);

		// set selection to previous task and if that fails then next task
		if (!GotoNextTask(TDCG_PREV) && !GotoNextTask(TDCG_NEXT))
		{
			TSH().RemoveAll();
		}
		
		// then delete and remove from undo
		{
			CHoldRedraw hr(m_taskTree);
			m_taskTree.DeleteItem(hti);

			m_data.DeleteTask(m_dwLastAddedID, FALSE); // FALSE == no undo
			m_data.DeleteLastUndoAction();
		}

		CDWordArray aModTaskIDs;
		aModTaskIDs.Add(m_dwLastAddedID);

		SetModified(TDCA_DELETE, aModTaskIDs);
		UpdateControls();
	}

	SetFocusToTasks();
	SetEditTitleTaskID(0);

	return 0L;
}

BOOL CToDoCtrl::ConfirmDeleteAllTasks(BOOL bSelected) const
{
	if (!GetTaskCount() || !CanEditSelectedTask(TDCA_DELETE))
	{
		return FALSE;
	}
	// are we an archive and should we warn user if we are
	else if (m_bArchive && HasStyle(TDCS_WARNADDDELETEARCHIVE))
	{
		return (CMessageBox::AfxShow(IDS_TDC_CONFIRMDELETE_TITLE, IDS_TDC_WARNDELETEFROMARCHIVE, MB_YESNO | MB_ICONQUESTION) == IDYES);
	}
	else if (HasStyle(TDCS_CONFIRMDELETE))
	{
		CEnString sMessage(bSelected ? IDS_TDC_CONFIRMDELETESEL : IDS_TDC_CONFIRMDELETEALL);
		UINT nFlags = (MB_ICONEXCLAMATION | MB_YESNO);

		if (bSelected && m_taskTree.SelectionHasSubtasks())
		{
			if (GetSelectedCount() == 1)
				sMessage += CEnString(IDS_TDC_CONFIRMDELETEONEWARNSUBTASKS);
			else
				sMessage += CEnString(IDS_TDC_CONFIRMDELETEMOREWARNSUBTASKS);

			nFlags |= MB_DEFBUTTON2;
		}
		// else MB_DEFBUTTON1
			
		return (CMessageBox::AfxShow(IDS_TDC_CONFIRMDELETE_TITLE, sMessage, nFlags) == IDYES);
	}
		
	// all else
	return TRUE;
}

BOOL CToDoCtrl::DeleteAllTasks()
{
	if (!ConfirmDeleteAllTasks())
		return FALSE;

	// must do these first
	Flush();
	m_taskTree.DeselectAll();
	
	CWaitCursor cursor;
	
	IMPLEMENT_DATA_UNDO(m_data, TDCUAT_DELETE);
	HOLD_REDRAW(*this, 0);

	m_taskTree.DeleteAll();
	m_data.DeleteAllTasks();

	SetModified(TDCA_DELETE);
	UpdateControls();
	
	return TRUE;
}

DWORD CToDoCtrl::SetStyle(TDC_STYLE nStyle, BOOL bEnable)
{
	ASSERT (GetSafeHwnd());
	
	DWORD dwResult = 0;
	
	m_styles[nStyle] = bEnable;

	switch (nStyle)
	{
	case TDCS_SHOWDEFAULTTASKICONS:
		LoadTaskIcons();
		break;

	case TDCS_SHOWDATESINISO:
		{
			DWORD dwStyle = m_cbTimeDue.GetStyle();
			Misc::SetFlag(dwStyle, TCB_ISO, bEnable);

			m_cbTimeStart.SetStyle(dwStyle);
			m_cbTimeDue.SetStyle(dwStyle);
			m_cbTimeDone.SetStyle(dwStyle);
		}
		break;

	case TDCS_NODUEDATEISDUETODAYORSTART:
	case TDCS_SHOWPATHINHEADER:
	case TDCS_SHOWCOMMENTSINLIST:
	case TDCS_SHOWFIRSTCOMMENTLINEINLIST:
	case TDCS_STRIKETHOUGHDONETASKS:
	case TDCS_RIGHTSIDECOLUMNS:
	case TDCS_DISPLAYHMSTIMEFORMAT:
	case TDCS_TREATSUBCOMPLETEDASDONE:
	case TDCS_CALCREMAININGTIMEBYDUEDATE:
	case TDCS_CALCREMAININGTIMEBYSPENT:
	case TDCS_CALCREMAININGTIMEBYPERCENT:
	case TDCS_USEHIGHESTPRIORITY:
	case TDCS_INCLUDEDONEINPRIORITYCALC:
	case TDCS_HIDEPRIORITYNUMBER:
	case TDCS_USEHIGHESTRISK:
	case TDCS_INCLUDEDONEINRISKCALC:
	case TDCS_SHOWNONFILEREFSASTEXT:
	case TDCS_USEPERCENTDONEINTIMEEST:
	case TDCS_HIDEZEROTIMECOST:
	case TDCS_ROUNDTIMEFRACTIONS:
	case TDCS_HIDEPERCENTFORDONETASKS:
	case TDCS_INCLUDEDONEINAVERAGECALC:
	case TDCS_WEIGHTPERCENTCALCBYNUMSUB:
	case TDCS_SHOWPERCENTASPROGRESSBAR:
	case TDCS_HIDEZEROPERCENTDONE:
	case TDCS_AVERAGEPERCENTSUBCOMPLETION:
	case TDCS_AUTOCALCPERCENTDONE:
	case TDCS_HIDESTARTDUEFORDONETASKS:
	case TDCS_SHOWWEEKDAYINDATES:
	case TDCS_SORTDONETASKSATBOTTOM:
	case TDCS_DUEHAVEHIGHESTPRIORITY:
	case TDCS_DONEHAVELOWESTPRIORITY:
	case TDCS_DONEHAVELOWESTRISK:
		// handled solely by tree-list
		break;

	case TDCS_SHOWINFOTIPS:
		if (bEnable)
		{
			ASSERT(!m_infoTip.GetSafeHwnd());

			if (!m_infoTip.Create(this))
				return FALSE;

			// else
			m_infoTip.ModifyStyleEx(0, WS_EX_TRANSPARENT);
			m_infoTip.SetDelayTime(TTDT_INITIAL, 50);
			m_infoTip.SetDelayTime(TTDT_AUTOPOP, 100000);
			m_infoTip.SetMaxTipWidth(SHRT_MAX); // multiline support
			m_infoTip.EnableTracking(TRUE, INFOTIPOFFSET, INFOTIPOFFSET);
			m_infoTip.SetFont(CFont::FromHandle(m_hFontTree));
		}
		else if (m_infoTip.GetSafeHwnd())
		{
			m_infoTip.DestroyWindow();
		}
		break;

	case TDCS_USEEARLIESTDUEDATE:
		if (bEnable)
			m_styles[TDCS_USELATESTDUEDATE] = FALSE; // mutually exclusive
		break;

	case TDCS_USELATESTDUEDATE:
		if (bEnable)
			m_styles[TDCS_USEEARLIESTDUEDATE] = FALSE; // mutually exclusive
		break;

	case TDCS_USEEARLIESTSTARTDATE:
		if (bEnable)
			m_styles[TDCS_USELATESTSTARTDATE] = FALSE; // mutually exclusive
		break;

	case TDCS_USELATESTSTARTDATE:
		if (bEnable)
			m_styles[TDCS_USEEARLIESTSTARTDATE] = FALSE; // mutually exclusive
		break;

	case TDCS_TASKCOLORISBACKGROUND:
		if (bEnable)
		{
			m_cpColour.SetSelectionMode(CP_MODE_BK);
			m_cpColour.SetBkColour(m_cpColour.GetTextColour());
			m_cpColour.SetTextColour(CLR_DEFAULT);
		}
		else
		{
			m_cpColour.SetSelectionMode(CP_MODE_TEXT);
			m_cpColour.SetTextColour(m_cpColour.GetBkColour());
			m_cpColour.SetBkColour(CLR_DEFAULT);
		}
		break;

	case TDCS_SHOWPROJECTNAME:
		// Fix up focus
		if (!bEnable && (GetFocus() == GetDlgItem(IDC_PROJECTNAME)))
		{
			SetFocusToTasks();
		}
		dwResult = TDCSS_WANTRESIZE;
		break;

	case TDCS_SHOWCOMMENTSALWAYS:
	case TDCS_AUTOREPOSCTRLS:
	case TDCS_COLORTEXTBYPRIORITY:
	case TDCS_COLORTEXTBYATTRIBUTE:
	case TDCS_COLORTEXTBYNONE:
	case TDCS_ALLOWCOMMENTSSTACKING:
	case TDCS_STACKCOMMENTSABOVEEDITS:
		dwResult = TDCSS_WANTRESIZE;
		break;

	case TDCS_READONLY:
		m_treeDragDrop.EnableDragDrop(!bEnable);
		break;

	case TDCS_CONFIRMDELETE:
	case TDCS_CHECKOUTONLOAD:
	case TDCS_SYNCTIMEESTIMATESANDDATES:
	case TDCS_FOCUSTREEONENTER:
	case TDCS_LOGTIMETRACKING:
	case TDCS_LOGTASKTIMESEPARATELY:
	case TDCS_WARNADDDELETEARCHIVE:
	case TDCS_ALLOWPARENTTIMETRACKING:
	case TDCS_USES3RDPARTYSOURCECONTROL:
	case TDCS_REFILTERONMODIFY:
	case TDCS_SHOWPARENTSASFOLDERS:
	case TDCS_INCLUDEUSERINCHECKOUT:
	case TDCS_DISPLAYLOGCONFIRM:
	case TDCS_SYNCCOMPLETIONTOSTATUS:
	case TDCS_DISABLEPASSWORDPROMPTING:
	case TDCS_SAVEUIVISINTASKLIST:
	case TDCS_AUTOADJUSTDEPENDENCYDATES:
	case TDCS_TRACKSELECTEDTASKONLY:
	case TDCS_COMMENTSUSETREEFONT:
		// do nothing
		break;

	default:
		//ASSERT(0); // just to help catch forgotten styles
		break;
	}
	
	return dwResult;
}

int CToDoCtrl::OnToolHitTest(CPoint point, TOOLINFO * pTI) const
{
	// Don't yet know why but updating the info tip text while
	// multiple selecting causes the mouse-down message to get
	// mislaid/eaten and the multiple-selection fails so if the
	// ctrl or shift keys are down we don't return a tooltip
	if (HasStyle(TDCS_SHOWINFOTIPS) && 
		m_infoTip.GetSafeHwnd() &&
		(m_nMaxState != TDCMS_MAXCOMMENTS) && 
		!Misc::IsKeyPressed(VK_CONTROL) &&
		!Misc::IsKeyPressed(VK_SHIFT))
	{
		CWnd::ClientToScreen(&point);

		DWORD dwTaskID = HitTestTask(point, TRUE);

		if (dwTaskID)
		{
			CTDCAttributeMap mapAttrib;
			TDC::MapColumnsToAttributes(m_visColEdit.GetVisibleColumns(), mapAttrib);

			if (m_nMaxState == TDCMS_NORMAL)
				mapAttrib.Add(TDCA_COMMENTS);

			CString sInfoTip = m_infoTip.FormatTip(dwTaskID, 
												   mapAttrib, 
												   m_nMaxInfotipCommentsLength);
			ASSERT(!sInfoTip.IsEmpty());

			HWND hwndHit = CDialogHelper::GetWindowFromPoint(GetSafeHwnd(), point);
			ASSERT(hwndHit);

			return CToolTipCtrlEx::SetToolInfo(*pTI, hwndHit, sInfoTip, dwTaskID);
		}
	}

	return CWnd::OnToolHitTest(point, pTI);
}

void CToDoCtrl::SetReadonly(BOOL bReadOnly)
{
	BOOL bIsReadOnly = HasStyle(TDCS_READONLY);

	if ((bIsReadOnly && !bReadOnly) || (!bIsReadOnly && bReadOnly))
		ModifyStyles(CTDCStyleMap(TDCS_READONLY, bReadOnly));
}

BOOL CToDoCtrl::ModifyStyles(const CTDCStyleMap& modStyles)
{
	if (modStyles.GetCount() == 0)
		return FALSE;

	HandleUnsavedComments();

	CHoldRedraw hr(*this, (NCR_PAINT | NCR_ERASEBKGND));
	
	POSITION pos = modStyles.GetStartPosition();
	DWORD dwResult = 0;

	while (pos)
	{
		TDC_STYLE nStyle;
		BOOL bWantOn;
		modStyles.GetNextAssoc(pos, nStyle, bWantOn);

		dwResult |= SetStyle(nStyle, bWantOn);
	}

	// notify tree-list we've finished changing styles
	OnStylesUpdated(modStyles);

	if (Misc::HasFlag(dwResult, TDCSS_WANTRESIZE)) 
		Resize();

	Invalidate(TRUE);
	UpdateControls();

	return TRUE;
}

void CToDoCtrl::Resort(BOOL bAllowToggle)
{
	if (IsResortAllowed())
		m_taskTree.Resort(bAllowToggle);
}

BOOL CToDoCtrl::HasStyle(TDC_STYLE nStyle) const 
{ 
	// some special handling for dependent styles
	switch (nStyle)
	{
	case TDCS_SHOWFIRSTCOMMENTLINEINLIST:
		if (!HasStyle(TDCS_SHOWCOMMENTSINLIST))
            return FALSE;
		break;

	case TDCS_LOGTASKTIMESEPARATELY:
		if (!HasStyle(TDCS_LOGTIMETRACKING))
            return FALSE;
		break;
	}
	
	return m_styles.IsStyleEnabled(nStyle); 
}

void CToDoCtrl::SetCompletionStatus(const CString& sStatus) 
{ 
	if (sStatus != m_sCompletionStatus)
	{
		m_sCompletionStatus = sStatus; 
		m_taskTree.SetCompletionStatus(sStatus);
	}
}

void CToDoCtrl::SetLayoutPositions(TDC_UILOCATION nControlsPos, TDC_UILOCATION nCommentsPos, BOOL bResize)
{
	BOOL bChanged = (nControlsPos != m_nControlsPos || nCommentsPos != m_nCommentsPos);

	m_nControlsPos = nControlsPos;
	m_nCommentsPos = nCommentsPos;

	if (bChanged && bResize)
		Resize();
}

int CToDoCtrl::GetCustomAttributeDefs(CTDCCustomAttribDefinitionArray& aAttrib) const
{
	aAttrib.Copy(m_aCustomAttribDefs);
	return aAttrib.GetSize();
}

BOOL CToDoCtrl::SetCustomAttributeDefs(const CTDCCustomAttribDefinitionArray& aAttrib)
{
	ASSERT(CanEditSelectedTask(TDCA_CUSTOMATTRIBDEFS));

	if (!Misc::MatchAllT(m_aCustomAttribDefs, aAttrib, FALSE))
	{
		m_aCustomAttribDefs.Copy(aAttrib);

		RebuildCustomAttributeUI();
		UpdateDefaultTaskCustomAttributeValues();

		// update interface
		SetModified(TDCA_CUSTOMATTRIBDEFS);
		UpdateControls(FALSE); // don't update comments

		return TRUE;
	}

	// else
	return FALSE;
}

void CToDoCtrl::UpdateVisibleColumns(const CTDCColumnIDMap& mapChanges)
{
	m_taskTree.OnColumnVisibilityChange(mapChanges);
}

void CToDoCtrl::SetColumnFieldVisibility(const TDCCOLEDITVISIBILITY& vis)
{
	BOOL bColumnChange, bEditChange;
	BOOL bChange = m_visColEdit.HasDifferences(vis, bColumnChange, bEditChange);

	if (!bChange)
		return;

	TDCCOLEDITVISIBILITY visPrev = m_visColEdit;
	m_visColEdit = vis;

	if (bColumnChange)
	{
		CTDCColumnIDMap mapChanges;
		VERIFY(visPrev.GetVisibleColumns().GetDifferences(vis.GetVisibleColumns(), mapChanges));

		UpdateVisibleColumns(mapChanges);
	}
		
	// hide/show controls which may have been affected
	if (bEditChange || (bColumnChange && (vis.GetShowFields() == TDLSA_ASCOLUMN)))
	{		
		Invalidate();
		Resize();
		UpdateControls(FALSE); // don't update comments
	}

	if (bChange && HasStyle(TDCS_SAVEUIVISINTASKLIST))
		m_bModified = TRUE;
}

void CToDoCtrl::GetColumnFieldVisibility(TDCCOLEDITVISIBILITY& vis) const
{
	vis = m_visColEdit;
}

const CTDCColumnIDMap& CToDoCtrl::GetVisibleColumns() const
{
	return m_visColEdit.GetVisibleColumns();
}

const CTDCAttributeMap& CToDoCtrl::GetVisibleEditFields() const
{
	return m_visColEdit.GetVisibleEditFields();
}

int CToDoCtrl::GetSortableColumns(CTDCColumnIDMap& mapColIDs) const
{
	mapColIDs.Copy(m_visColEdit.GetVisibleColumns());
	mapColIDs.Add(TDCC_CLIENT); // always

	for (int nAttrib = 0; nAttrib < m_aCustomAttribDefs.GetSize(); nAttrib++)
	{
		const TDCCUSTOMATTRIBUTEDEFINITION& attribDef = m_aCustomAttribDefs[nAttrib];

		if (attribDef.bEnabled && attribDef.SupportsFeature(TDCCAF_SORT))
			mapColIDs.Add(attribDef.GetColumnID());
	}

	return mapColIDs.GetCount();
}

BOOL CToDoCtrl::IsColumnShowing(TDC_COLUMN nColumn) const
{
	if ((nColumn == TDCC_CLIENT) || TDCCUSTOMATTRIBUTEDEFINITION::IsCustomColumn(nColumn))
		return TRUE;

	return m_visColEdit.IsColumnVisible(nColumn);
}

BOOL CToDoCtrl::IsEditFieldShowing(TDC_ATTRIBUTE nAttrib) const
{
	if (nAttrib == TDCA_TASKNAME || TDCCUSTOMATTRIBUTEDEFINITION::IsCustomAttribute(nAttrib))
		return TRUE; // always visible

	return m_visColEdit.IsEditFieldVisible(nAttrib);
}

BOOL CToDoCtrl::IsColumnOrEditFieldShowing(TDC_COLUMN nColumn, TDC_ATTRIBUTE nAttrib) const
{
	return (IsColumnShowing(nColumn) || IsEditFieldShowing(nAttrib));
}

TDC_FILE CToDoCtrl::Save(const CString& sFilePath, BOOL bFlush)
{
	CTaskFile tasks;

	return Save(tasks, sFilePath, bFlush);
}

TDC_FILE CToDoCtrl::Save(CTaskFile& tasks/*out*/, const CString& sFilePath, BOOL bFlush)
{
	// PERMANENT LOGGING //////////////////////////////////////////////
	CScopedLogTimer log(_T("CToDoCtrl::Save(%s)"), FileMisc::GetFileNameFromPath(sFilePath));
	///////////////////////////////////////////////////////////////////////
	
	ASSERT (GetSafeHwnd());
	
	if (!GetSafeHwnd())
		return TDCF_OTHER;

	// can't save if file is readonly
	if (CDriveInfo::IsReadonlyPath(sFilePath) > 0)
	{
		return TDCF_NOTALLOWED;
	}
	
	// can't save if not checked-out
	// unless we're saving to another filename or this is our first save
	if (m_sourceControl.IsSourceControlled() && !m_sourceControl.IsCheckedOut())
	{
		BOOL bFirstSave = (!HasFilePath() || !FileMisc::IsSamePath(m_sLastSavePath, sFilePath));
		
		if (!bFirstSave)
			return TDCF_SSC_NOTCHECKEDOUT;
	}

	CString sSavePath(sFilePath);
	
	if (sSavePath.IsEmpty())
	{
		if (!HasFilePath())
			return TDCF_OTHER;
		else
			sSavePath = m_sLastSavePath;
	}

	if (bFlush)
		Flush();

	// check for later changes if it's a network file
	BOOL bCheckforLaterChanges = (CDriveInfo::IsRemotePath(sFilePath) > 0);
	
	if (bCheckforLaterChanges && (m_nFileVersion > 0)) // else its newly created
	{
		if (FileMisc::FileExists(sSavePath)) // file exists (sanity check)
		{
			CTaskFile temp;

			if (temp.LoadHeader(sSavePath) && (temp.GetFileVersion() > m_nFileVersion))
			{
				CEnString sMessage(IDS_TDC_CONFIRMOVERWRITE, sSavePath);

				if (CMessageBox::AfxShow(IDS_TDC_CONFIRMOVERWRITE_TITLE, sMessage, MB_ICONWARNING | MB_YESNO) != IDYES)
				{
					return TDCF_CANCELLED;
				}
			}
		}
	}

	// prepare task file
	if (IsModified())
		m_nFileVersion++;

	BuildTasksForSave(tasks);

	TDC_FILE nResult = SaveTaskfile(tasks, sSavePath);

	if (nResult == TDCF_SUCCESS)
	{
		SetFilePath(sSavePath);

		m_bModified = FALSE;
	}

	return nResult;
}

// static helper
TDC_FILE CToDoCtrl::SaveTaskfile(CTaskFile& tasks, const CString& sSavePath)
{
	// PERMANENT LOGGING //////////////////////////////////////////////
	CScopedLogTimer log(_T("CToDoCtrl::SaveTaskfile(%s)"), FileMisc::GetFileNameFromPath(sSavePath));
	///////////////////////////////////////////////////////////////////
	CWaitCursor cursor;

	// Always backup before overwriting
	CTempFileBackup backup(sSavePath);
	
	// do the save
	if (tasks.Save(sSavePath, SFEF_UTF16))
		return TDCF_SUCCESS;

	// Error
	VERIFY(backup.RestoreBackup());
	
	return MapTaskfileError(tasks.GetLastFileError());
}

// static helper
TDC_FILE CToDoCtrl::MapTaskfileError(int nFileErr)
{
	switch (nFileErr)
	{
	case XFL_CANCELLED:			return TDCF_CANCELLED;
	case XFL_MISSINGROOT:		return TDCF_NOTTASKLIST;
	case XFL_BADMSXML:			return TDCF_BADMSXML;
	case XFL_NOENCRYPTIONDLL:	return TDCF_NOENCRYPTIONDLL;
	case XFL_UNKNOWNENCRYPTION:	return TDCF_UNKNOWNENCRYPTION;
		
	default:
		// if nFileErr is greater than zero then it represents GetLastError
		// so we append this to TDCO_OTHER
		if (nFileErr > 0)
		{
			switch (nFileErr)
			{
			case ERROR_ACCESS_DENIED:		return TDCF_NOTALLOWED;
			case ERROR_SHARING_VIOLATION:	return TDCF_INUSE;
			case ERROR_DISK_FULL:			return TDCF_NOSPACE;
				
			default: return (TDC_FILE)(TDCF_OTHER + nFileErr);
			}
		}
	}
	
	// all else
	return TDCF_OTHER;
}

void CToDoCtrl::BuildTasksForSave(CTaskFile& tasks) const
{
	// PERMANENT LOGGING //////////////////////////////////////////////
	CScopedLogTimer log(_T("CToDoCtrl::BuildTasksForSave()"));
	///////////////////////////////////////////////////////////////////

	tasks.Reset();

	// get tasklist
	GetAllTasks(tasks);

	// save globals
	SaveGlobals(tasks);

	// attrib visibility
	SaveAttributeVisibility(tasks);

	// and meta data
	tasks.SetMetaData(m_mapMetaData);

	// And password prompting disability
	if (IsEncrypted() && HasStyle(TDCS_DISABLEPASSWORDPROMPTING))
		tasks.SetDisablePasswordPrompting();
	
	// encrypt prior to setting checkout status and file info 
	// so these are visible without decryption
	// this quietly  fails if password is empty
	tasks.Encrypt(m_sPassword, SFEF_UTF16);

	// then append header info
	AppendTaskFileHeader(tasks);
	
	// checkout status
	m_sourceControl.PrepareTasksForSave(tasks);
}

void CToDoCtrl::SaveGlobals(CTaskFile& tasks) const
{
	// Remove default items before saving
	TDCAUTOLISTDATA tld(m_tldAll, TDCA_ALL);
	tld.RemoveItems(m_tldDefault, TDCA_ALL);

	tasks.SetAutoListData(tld);
}

void CToDoCtrl::LoadGlobals(const CTaskFile& tasks)
{
	TDCAUTOLISTDATA tld;

	if (tasks.GetAutoListData(tld))
	{
		int nNumAdded = 0;

		nNumAdded += AddUserListContent(m_cbCategory,	tld.aCategory);
		nNumAdded += AddUserListContent(m_cbTags,		tld.aTags);
		nNumAdded += AddUserListContent(m_cbStatus,		tld.aStatus);
		nNumAdded += AddUserListContent(m_cbAllocTo,	tld.aAllocTo);
		nNumAdded += AddUserListContent(m_cbAllocBy,	tld.aAllocBy);
		nNumAdded += AddUserListContent(m_cbVersion,	tld.aVersion);

		if (nNumAdded)
			UpdateAutoListData();
	}
}

int CToDoCtrl::AddUserListContent(CAutoComboBox& combo, const CStringArray& aItems)
{
	// we only bother updating the combos if their content is 'writable'
	if (CDialogHelper::ComboHasEdit(combo))
		return combo.AddUniqueItems(aItems);

	// else
	return 0; // nothing added
}

void CToDoCtrl::SaveCustomAttributeDefinitions(CTaskFile& tasks, const TDCGETTASKS& filter) const
{
	// save auto combobox contents to definition first
	// just like we do with standard combos
	CTDCCustomAttributeUIHelper::SaveAutoListDataToDefs(this, m_aCustomControls, m_aCustomAttribDefs);

	if (filter.mapAttribs.HasOnly(TDCA_ALL))
	{
		tasks.SetCustomAttributeDefs(m_aCustomAttribDefs);
	}
	else // save all
	{
		// Save only those attributes that exist in the filter 
		CTDCCustomAttribDefinitionArray aAttribDefs;
		aAttribDefs.Copy(m_aCustomAttribDefs);

		int nDef = aAttribDefs.GetSize();

		while (nDef--)
		{
			if (!filter.WantAttribute(aAttribDefs[nDef].GetAttributeID()))
				aAttribDefs.RemoveAt(nDef);
		}

		if (aAttribDefs.GetSize() > 0)
			tasks.SetCustomAttributeDefs(aAttribDefs);
	}
}

void CToDoCtrl::LoadCustomAttributeDefinitions(const CTaskFile& tasks)
{
	tasks.GetCustomAttributeDefs(m_aCustomAttribDefs);

	// Add Fields and columns to view
	RebuildCustomAttributeUI();
}

void CToDoCtrl::RebuildCustomAttributeUI()
{
	// and add fields after the 'version' control
	CTDCCustomAttributeUIHelper::RebuildEditControls(this,
													 m_aCustomAttribDefs,
													 m_ilTaskIcons,
													 IDC_VERSION,
													 m_aCustomControls);

	CTDCCustomAttributeUIHelper::AddWindowPrompts(m_aCustomControls, this, m_mgrPrompts);

	Resize();

	m_taskTree.OnCustomAttributeChange();
}

BOOL CToDoCtrl::CheckRestoreBackupFile(const CString& sFilePath)
{
	// check for the existence of a backup file
	CString sBackupPath = CFileBackup::BuildBackupPath(sFilePath, FBS_OVERWRITE);
	
	if (FileMisc::FileExists(sBackupPath))
	{
		FileMisc::LogText(_T("Backup file found: %s"), sBackupPath);

		double dBackupSize = FileMisc::GetFileSize(sBackupPath);
		
		// Check for valid backup
		if (dBackupSize > 0.0)
		{
			FileMisc::LogText(_T("  Backup file has non-zero length"));

			double dSize = FileMisc::GetFileSize(sFilePath);
		
			if (dSize == 0.0) // definitely a bad save -> copy over backup
			{
				FileMisc::LogText(_T("    Taskfile '%s' has zero length, replacing with backup"));

				FileMisc::DeleteFile(sFilePath, TRUE);
				FileMisc::MoveFile(sBackupPath, sFilePath);
			}
			else
			{
				FileMisc::LogText(_T("    Taskfile has non-zero length"));

				time64_t tMod = FileMisc::GetFileLastModified(sFilePath);
				time64_t tBackupMod = FileMisc::GetFileLastModified(sBackupPath);

				if (tMod >= tBackupMod) // file is newer than backup
				{
					FileMisc::LogText(_T("      Taskfile is newer than backup, deleting backup"));

					FileMisc::DeleteFile(sBackupPath, TRUE);
				}
				else // Different sizes and dates -> prompt
				{
					CEnString sMessage(IDS_BACKUPFILEFOUND, sFilePath);
					int nRet = AfxMessageBox(sMessage, MB_YESNOCANCEL);
			
					switch (nRet)
					{
					case IDYES:
						FileMisc::LogText(_T("      User confirmed to replace taskfile with backup"));

						FileMisc::DeleteFile(sFilePath, TRUE);
						FileMisc::MoveFile(sBackupPath, sFilePath, TRUE, TRUE);
						break;
				
					case IDNO: // keep the backup just in case
						{
							FileMisc::LogText(_T("      User confirmed to keep taskfile, renaming backup"));

							CString sRename = CFileBackup::BuildBackupPath(sBackupPath);
							FileMisc::MoveFile(sBackupPath, sRename, TRUE, TRUE);
						}
						break;
				
					case IDCANCEL:
						return FALSE; // do nothing
					}
				}
			}
		}
		else
		{
			FileMisc::LogText(_T("  Backup file has zero length, deleting backup"));

			FileMisc::DeleteFile(sBackupPath, TRUE);
		}
	}
	
	return TRUE;
}

// thin wrapper
TDC_FILE CToDoCtrl::Load(const CString& sFilePath)
{
	CTaskFile file;
	return Load(sFilePath, file);
}

TDC_FILE CToDoCtrl::Load(const CString& sFilePath, CTaskFile& tasks/*out*/)
{
	ASSERT (GetSafeHwnd());
	
	if (!GetSafeHwnd())
		return TDCF_OTHER;
	
	if (!CheckRestoreBackupFile(sFilePath))
		return TDCF_CANCELLED;

	if (!FileMisc::FileExists(sFilePath))
		return TDCF_NOTEXIST;

	SetReadonly(CDriveInfo::IsReadonlyPath(sFilePath) > 0);

	// PERMANENT LOGGING //////////////////////////////////////////////
	CScopedLogTimer log(_T("CToDoCtrl::Load(%s)"), FileMisc::GetFileNameFromPath(sFilePath));
	log.LogStart();
	///////////////////////////////////////////////////////////////////
	
	if (tasks.Load(sFilePath, NULL, FALSE)) // don't decrypt
	{
		log.LogTimeElapsed(_T("CToDoCtrl::Load(tasks.Load)"), FileMisc::GetFileNameFromPath(sFilePath));

		m_sourceControl.InitialiseState(tasks);

		BOOL bWantCheckout = (HasStyle(TDCS_CHECKOUTONLOAD) &&
								m_sourceControl.IsSourceControlled() && 
								!m_sourceControl.IsCheckedOut());

		if (bWantCheckout)
		{
			// Checkout has to happen before the tasks are decrypted
			// but it will fail if we are currently delay loaded.
			// And m_bDelayLoaded is not cleared until LoadTasks(...)
			// which happens only after decryption.
			// So we temporarily clear m_bDelayLoaded just for this case.
			CAutoFlag af(m_bDelayLoaded, FALSE);

			// Clear flag if check-out failed
			bWantCheckout = (m_sourceControl.CheckOut(tasks, CString(), FALSE, sFilePath) == TDCF_SUCCESS);
		}

		if (tasks.Decrypt(m_sPassword))
		{
			// save off password
			m_sPassword = tasks.GetPassword();
			
			// get comments type from header of older tasklists for the
			// setting of the enclosed tasks
			CONTENTFORMAT cf = tasks.GetCommentsType();

			if (!cf.IsEmpty())
				m_cfComments = cf;

			// meta data
			tasks.GetMetaData(m_mapMetaData);

			// and selection history unless reloading
			if (m_sLastSavePath != sFilePath)
			{
				TSH().RemoveAll();
				TSH().ClearHistory();
			}

			LoadTasks(tasks);
					
			// PERMANENT LOGGING //////////////////////////////////////////////
			log.LogTimeElapsed(_T("CToDoCtrl::Load(LoadTasks)"));
			///////////////////////////////////////////////////////////////////

			LoadTaskIcons();
					
			// PERMANENT LOGGING //////////////////////////////////////////////
			log.LogTimeElapsed(_T("CToDoCtrl::Load(LoadTaskIcons)"));
			///////////////////////////////////////////////////////////////////
			
			SetModified(FALSE);
			return TDCF_SUCCESS;
		} 
		// user cancelled so revert any check-out
		else if (bWantCheckout)
		{
			VERIFY(m_sourceControl.CheckIn(tasks) == TDCF_SUCCESS);
		}
	}

	// else do error handling
	return MapTaskfileError(tasks.GetLastFileError());
}

BOOL CToDoCtrl::DelayLoad(const CString& sFilePath, COleDateTime& dtEarliestDue)
{
	ASSERT (m_bDelayLoaded || !HasFilePath());

	m_bDelayLoaded = FALSE;
	CTaskFile temp;
	
	if (temp.LoadHeader(sFilePath) && (temp.IsArchive() || temp.GetEarliestTaskDueDate(dtEarliestDue)))
	{
		m_bDelayLoaded = TRUE;

		// save off some of the header info
		SetFilePath(sFilePath);
		SetProjectName(temp.GetProjectName());
		SetModified(FALSE);

		temp.GetCustomAttributeDefs(m_aCustomAttribDefs);
	}

	return m_bDelayLoaded;
}

BOOL CToDoCtrl::LoadTasks(const CTaskFile& tasks)
{
	ASSERT (GetSafeHwnd());
	
	if (!GetSafeHwnd())
		return FALSE;

	m_taskTree.EnableRecalcColumns(FALSE);

	// PERMANENT LOGGING //////////////////////////////////////////////
	CScopedLogTimer log;
	///////////////////////////////////////////////////////////////////

	// save visible state
	BOOL bHidden = !IsWindowVisible();

	// save current state if reloading same file
	TDCSELECTIONCACHE cache;
	CPreferences prefs;

	if (!m_bDelayLoaded && (m_sLastSavePath == tasks.GetFilePath()))
	{
		CacheTreeSelection(cache);

		SaveTasksState(prefs);
		SaveSplitPos(prefs);
		SaveAttributeVisibility(prefs);
		m_findReplace.SaveState(prefs);

		// PERMANENT LOGGING //////////////////////////////////////////////
		log.LogTimeElapsed(_T("CToDoCtrl::LoadTasks(Save state)"));
		///////////////////////////////////////////////////////////////////
	}	
	
	// Update XML headers if not already unicode
	m_sXslHeader = tasks.GetXslHeader();
	m_sXmlHeader = tasks.GetXmlHeader();

	if (!XMLHeaderIsUnicode(m_sXmlHeader))
		m_sXmlHeader = DEFAULT_UNICODE_HEADER;

	// file header info
	m_nFileFormat = tasks.GetFileFormat();
	m_nFileVersion = tasks.GetFileVersion();
	m_sProjectName = tasks.GetProjectName();
	m_bArchive = tasks.IsArchive();
	m_dtLastTaskMod = tasks.GetLastModifiedOle();
	
	// next unique ID
	m_dwNextUniqueID = tasks.GetNextUniqueID();
	
	// backwards compatibility
	if (!m_dwNextUniqueID)
		m_dwNextUniqueID = 1;
	
	m_bDelayLoaded = FALSE;

	SetFilePath(tasks.GetFilePath());

	LoadGlobals(tasks);
	LoadCustomAttributeDefinitions(tasks);
	LoadSplitPos(prefs);
	LoadDefaultRecurrence(prefs);
	LoadAttributeVisibility(tasks, prefs);
	m_findReplace.LoadState(prefs);

	if (tasks.IsPasswordPromptingDisabled())
		m_styles[TDCS_DISABLEPASSWORDPROMPTING] = TRUE;
	
	// PERMANENT LOGGING //////////////////////////////////////////////
	log.LogTimeElapsed(_T("CToDoCtrl::LoadTasks(Process header)"));
	///////////////////////////////////////////////////////////////////

	if (tasks.GetTaskCount())
	{
		HOLD_REDRAW(*this, m_taskTree.Tree());
	
		HTREEITEM htiSel = NULL;
		DWORD dwFirstVis = GetTaskID(m_taskTree.Tree().GetFirstVisibleItem());
		HTREEITEM htiFirst = SetAllTasks(tasks);

		// PERMANENT LOGGING //////////////////////////////////////////////
		log.LogTimeElapsed(_T("CToDoCtrl::LoadTasks(Build tree)"));
		///////////////////////////////////////////////////////////////////

		if (m_taskTree.GetItemCount())
		{
			// restore last tree state
			htiSel = LoadTasksState(prefs);

			// PERMANENT LOGGING //////////////////////////////////////////////
			log.LogTimeElapsed(_T("CToDoCtrl::LoadTasks(Restore state)"));
			///////////////////////////////////////////////////////////////////
			
			// redo last sort
			if (IsSorting())
				Resort();
		}

		// scroll to first visible item
		if (dwFirstVis)
		{
			HTREEITEM htiFirstVis = m_taskTree.GetItem(dwFirstVis);
			
			if (htiFirstVis)
				m_taskTree.Tree().SelectSetFirstVisible(htiFirstVis);
		}
		
		// restore last selected state
		if (!cache.IsEmpty())
		{
			RestoreTreeSelection(cache);
		}
		else
		{
			if (!htiSel)
				htiSel = htiFirst;
			
			if (htiSel)
				SelectItem(htiSel);
		}
	}
	else
	{
		m_bModified = FALSE;
	}

	// reset last taskedit
	m_dtLastTaskMod = COleDateTime::GetCurrentTime();

	Resize();

	// restore previously visibility
	if (bHidden)
		ShowWindow(SW_HIDE);
	
	m_taskTree.EnableRecalcColumns(TRUE);

	// PERMANENT LOGGING //////////////////////////////////////////////
	log.LogTimeElapsed(_T("CToDoCtrl::LoadTasks(Remaining)"));
	///////////////////////////////////////////////////////////////////
	
	return TRUE;
}

int CToDoCtrl::GetArchivableTasks(CTaskFile& tasks, BOOL bSelectedOnly) const
{
	if (bSelectedOnly)
		return GetSelectedTasks(tasks, TDCGETTASKS(TDCGT_ALL, TDCGSTF_ALLPARENTS));

	// else
	return GetTasks(tasks, TDCGT_DONE);
}

void CToDoCtrl::RemoveArchivedTasks(const CTaskFile& tasks, TDC_ARCHIVE nRemove, BOOL bRemoveFlagged)
{
	if (IsReadOnly() || (nRemove == TDC_REMOVENONE) || !tasks.GetTaskCount())
		return; // nothing to do

	CPreferences prefs;
	SaveTasksState(prefs);
	
	IMPLEMENT_DATA_UNDO(m_data, TDCUAT_ARCHIVE);
	
	// clear selection
	TSH().RemoveAll();
	TCH().SelectItem(NULL);
	
	CHoldRedraw hr(m_taskTree);
	HTASKITEM hTask = tasks.GetFirstTask();
	BOOL bRemoved = FALSE;

	while (hTask)
	{
		bRemoved |= RemoveArchivedTask(tasks, hTask, nRemove, bRemoveFlagged);
		hTask = tasks.GetNextTask(hTask);
	}

	if (bRemoved)
		SetModified(TDCA_ARCHIVE);
}

BOOL CToDoCtrl::RemoveArchivedTask(const CTaskFile& tasks, HTASKITEM hTask, TDC_ARCHIVE nRemove, BOOL bRemoveFlagged)
{
	DWORD dwTaskID = tasks.GetTaskID(hTask);
	const TODOITEM* pTDI = NULL;
	const TODOSTRUCTURE* pTDS = NULL;

	if (!m_data.GetTrueTask(dwTaskID, pTDI, pTDS))
	{
		ASSERT(0);
		return FALSE;
	}

	BOOL bRemove = FALSE;
	BOOL bDone = m_calculator.IsTaskDone(pTDI, pTDS, TDCCHECKCHILDREN);

	// filter on completed tasks (and flagged tasks if requested)
	if (bDone)
	{
		// Note: it's not enough just to check this task's flagged state 
		// we need to do a deep search for flagged children and grandchildren, etc
		// otherwise they'll get removed when if we remove this task.
		if (!bRemoveFlagged)
		{
			CResultArray aResults;
			SEARCHPARAMS params;
			params.aRules.Add(SEARCHPARAM(TDCA_FLAG, FOP_SET));

			bRemove = !m_matcher.FindTasks(pTDI, pTDS, params, aResults, FALSE);
		}
		else
			bRemove = TRUE;

		if (bRemove)
		{
			bRemove = FALSE; // reset

			switch (nRemove)
			{
			case TDC_REMOVEALL:
				bRemove = TRUE;
				break;
				
			case TDC_REMOVEIFSIBLINGSANDSUBTASKSCOMPLETE:
				{
					TODOSTRUCTURE* pTDSParent = pTDS->GetParentTask();

					if (pTDSParent->IsRoot() || !m_data.TaskHasIncompleteSubtasks(pTDSParent, FALSE))
						bRemove = TRUE;
				}
				break;
				
			default:
				ASSERT (0);
			}
			
			if (bRemove && !RemoveArchivedTask(dwTaskID))
				return FALSE;
		}
	}
	
	// process children if not deleted
	if (!bRemove)
	{
		HTASKITEM hChildTask = tasks.GetFirstTask(hTask);
		
		while (hChildTask)
		{
			bRemove |= RemoveArchivedTask(tasks, hChildTask, nRemove, bRemoveFlagged);
			hChildTask = tasks.GetNextTask(hChildTask);
		}
	}

	return bRemove;
}

BOOL CToDoCtrl::RemoveArchivedTask(DWORD dwTaskID)
{
	HTREEITEM hti = m_taskTree.GetItem(dwTaskID);
	ASSERT(hti);
	
	if (!hti)
		return FALSE;
	
	m_taskTree.DeleteItem(hti);
	m_data.DeleteTask(dwTaskID, TRUE); // TRUE == with undo

	return TRUE;
}

BOOL CToDoCtrl::ArchiveDoneTasks(TDC_ARCHIVE nRemove, BOOL bRemoveFlagged)
{
	CString sArchivePath;
	
	if (!GetTaskCount() || !GetArchivePath(sArchivePath))
		return FALSE;
	
	// first see if there's anything to archive
	CTaskFile xiDone;
	
	if (!GetArchivableTasks(xiDone, FALSE))
		return FALSE;

	if (!ArchiveTasks(sArchivePath, xiDone))
		return FALSE;

	// remove archived tasks from current list
	TDCSELECTIONCACHE cache;
	CacheTreeSelection(cache);
	
	RemoveArchivedTasks(xiDone, nRemove, bRemoveFlagged);

	RestoreTreeSelection(cache);

	return TRUE;
}

BOOL CToDoCtrl::ArchiveSelectedTasks(BOOL bRemove)
{
	CString sArchivePath;

	if (!GetTaskCount() || !GetArchivePath(sArchivePath))
		return FALSE;
	
	// first see if there's anything to archive
	CTaskFile xiSel;
	
	if (!GetArchivableTasks(xiSel, TRUE))
		return FALSE;
	
	if (!ArchiveTasks(sArchivePath, xiSel))
		return FALSE;

	// delete the selected tasks
	if (bRemove)
	{
		IMPLEMENT_DATA_UNDO(m_data, TDCUAT_ARCHIVE);

		return DeleteSelectedTask(FALSE, TRUE);
	}

	// else
	return TRUE;
}

BOOL CToDoCtrl::ArchiveTasks(const CString& sArchivePath, const CTaskFile& tasks)
{
	// Can't archive if not checked out
	ASSERT(!IsReadOnly());

	// can't archive archives
	ASSERT(!m_bArchive);
	
	// must be something to archive
	ASSERT(tasks.GetTaskCount());

	// Time how long it takes
	DWORD dwStartTick = GetTickCount();
	
	// Load existing archive if present
	CTaskFile file(m_sPassword);
	TASKFILE_HEADER tfh;
	
	if (file.Load(sArchivePath))
	{
		VERIFY(file.IsArchive());

		// cache header for writing back
		file.GetHeader(tfh);
		tfh.dwNextID = 0; // will be set by taskfile

		if (!XMLHeaderIsUnicode(tfh.sXmlHeader))
			tfh.sXmlHeader = m_sXmlHeader;

		// increment file version if not 3rd party source controlled
		if (!HasStyle(TDCS_USES3RDPARTYSOURCECONTROL))
		{
			int nFileVer = (file.GetFileVersion() + 1);
			tfh.nFileVersion = max(nFileVer, 1);
		}
	}
	else // or initialize first time
	{
		tfh.bArchive = TRUE;
		tfh.sProjectName.Format(_T("%s (%s)"), m_sProjectName, CEnString(IDS_TDC_ARCHIVEPROJECT));
		tfh.nFileVersion = 1;
		tfh.sXmlHeader = m_sXmlHeader;
		tfh.sXslHeader = m_sXslHeader;
	}

	SaveCustomAttributeDefinitions(file);
	
	file.Merge(tasks, TRUE, TRUE);
	file.Encrypt();
	file.SetHeader(tfh);

	if (SaveTaskfile(file, sArchivePath) != TDCF_SUCCESS)
		return FALSE;

	// If the process took longer than 1 second we rename
	// the archive so that next time it'll start afresh
#ifndef _DEBUG
	if ((GetTickCount() - dwStartTick) > 1000)
	{
		int nSuffix = 1;
		CString sNewPath;

		while (true)
		{
			sNewPath = sArchivePath;
			VERIFY(FileMisc::AddToFileName(sNewPath, nSuffix++));

			if (!FileMisc::FileExists(sNewPath))
				break;

			// else keep trying
		} 
		ASSERT(!sNewPath.IsEmpty() && !FileMisc::FileExists(sNewPath));
		
		FileMisc::MoveFile(sArchivePath, sNewPath);
	}
#endif

	return TRUE;
}

HTREEITEM CToDoCtrl::PasteTaskToTree(const CTaskFile& tasks, HTASKITEM hTask, HTREEITEM htiParent, 
										HTREEITEM htiAfter, TDC_RESETIDS nResetID, BOOL bAndSubtasks)
{
	HTREEITEM hti = TVI_ROOT; // default for root item
	
	if (hTask)
	{
		// fixup htiAfter
		if (htiAfter == TVI_LAST)
			htiAfter = TCH().GetLastChildItem(htiParent);

		// add new task to map
		DWORD dwTaskID = tasks.GetTaskID(hTask);

		// make sure that m_dwNextUniqueID exceeds this ID
		if (dwTaskID && nResetID == TDCR_NO)
		{
			ASSERT (m_taskTree.GetItem(dwTaskID) == NULL); // sanity check
			m_dwNextUniqueID = max(m_dwNextUniqueID, dwTaskID + 1);
		}
		else if (dwTaskID && nResetID == TDCR_CHECK)
		{
			// see if it already exist
			if (m_data.HasTask(dwTaskID))
			{
				// provide a new unique ID
				dwTaskID = m_dwNextUniqueID++; 
			}
			else 
			{
				// make sure that m_dwNextUniqueID exceeds this ID
				m_dwNextUniqueID = max(m_dwNextUniqueID, dwTaskID + 1);
			}
		}
		else 
		{
			// always provide a new unique ID
			dwTaskID = m_dwNextUniqueID++;
		}
		
		// add items to map
		DWORD dwParentID = GetTaskID(htiParent);
		DWORD dwPrevTaskID = GetTaskID(htiAfter);

		TODOITEM* pTDI = m_data.NewTask(tasks, hTask, &m_tdiDefault);

		if (!pTDI)
		{
			ASSERT(0);
			return NULL;
		}
		
		// add this item to data structure
		m_data.AddTask(dwTaskID, pTDI, dwParentID, dwPrevTaskID);

		// And update inherited attributes
		m_data.ApplyLastInheritedChangeFromParent(dwTaskID, TDCA_ALL);
				
		// add this item to tree
		hti = InsertTreeItem(pTDI, dwTaskID, htiParent, htiAfter, TRUE);
	}

	HTREEITEM htiFirstItem = (hti == TVI_ROOT) ? NULL : hti;
	
	// then children
	if (bAndSubtasks)
	{
		HTASKITEM hChildTask = tasks.GetFirstTask(hTask);
	
		while (hChildTask)
		{
			HTREEITEM htiChild = PasteTaskToTree(tasks, hChildTask, hti, TVI_LAST, nResetID, bAndSubtasks);
		
			if (!htiFirstItem)
				htiFirstItem = htiChild;
		
			hChildTask = tasks.GetNextTask(hChildTask); // next
		}
	}
		
	return htiFirstItem;
}

HTREEITEM CToDoCtrl::InsertTreeItem(const TODOITEM* pTDI, DWORD dwTaskID, HTREEITEM htiParent, HTREEITEM htiAfter, BOOL bAddToCombos)
{
	HTREEITEM hti = m_taskTree.InsertItem(dwTaskID,	htiParent, htiAfter);
	ASSERT(hti);

	// add unique items to comboboxes
	if (bAddToCombos)
	{
		m_cbAllocTo.AddUniqueItems(pTDI->aAllocTo);
		m_cbAllocBy.AddUniqueItem(pTDI->sAllocBy);
		m_cbStatus.AddUniqueItem(pTDI->sStatus);
		m_cbCategory.AddUniqueItems(pTDI->aCategories);
		m_cbTags.AddUniqueItems(pTDI->aTags);
		m_cbVersion.AddUniqueItem(pTDI->sVersion);
	}

	return hti;
}

void CToDoCtrl::OnTreeChangeFocus(NMHDR* /*pNMHDR*/, LRESULT* pResult) 
{
	TSH().InvalidateAll(FALSE);
	*pResult = 0;
}

void CToDoCtrl::OnDestroy() 
{
	EndTimeTracking(FALSE, FALSE);
	
	// save state
	if (!m_bDelayLoaded)
	{
		CPreferences prefs;
		
		SaveTasksState(prefs);
		SaveSplitPos(prefs);
		SaveAttributeVisibility(prefs);
		SaveDefaultRecurrence(prefs);
		
		m_findReplace.SaveState(prefs);
		m_findReplace.DestroyDialog();
	}
	
	// clean up custom controls
	CTDCCustomAttributeUIHelper::CleanupControls(m_aCustomControls, this);
	
	CRuntimeDlg::OnDestroy();
}

void CToDoCtrl::SetModified(BOOL bMod)
{
	if (!IsReadOnly())
	{
		m_bModified = bMod;

		if (bMod)
		{
			m_dtLastTaskMod = COleDateTime::GetCurrentTime();
		}
		else if (m_nCommentsState == CS_PENDING)
		{
			m_nCommentsState = CS_CHANGED;
		}
	}
}

BOOL CToDoCtrl::IsModified() const 
{ 
	if (IsReadOnly())
		return FALSE;

	if (m_bModified)
		return TRUE;
	
	// else do we have outstanding unhandled comments
	return (m_nCommentsState == CS_PENDING);
}

void CToDoCtrl::SetModified(TDC_ATTRIBUTE nAttribID, const CDWordArray& aModTaskIDs)
{
	CTDCAttributeMap mapAttribIDs;
	GetAttributesAffectedByMod(nAttribID, mapAttribIDs);
	
	// Don't allow sorting during find/replace operation because
	// it messes up the order of the items	
	SetModified(mapAttribIDs, aModTaskIDs, !m_findReplace.IsReplacing());
}

void CToDoCtrl::SetModified(const CTDCAttributeMap& mapAttribIDs, const CDWordArray& aModTaskIDs, BOOL bAllowResort)
{
	if (IsReadOnly())
		return;
	
	SetModified(TRUE);
	
	m_taskTree.SetModified(mapAttribIDs, bAllowResort);

	TDCNOTIFYMOD mod(mapAttribIDs, aModTaskIDs);
	GetParent()->SendMessage(WM_TDCN_MODIFY, (WPARAM)GetSafeHwnd(), (LPARAM)&mod);

	// if this was the project name being edited make sure
	// the focus is set back to the name
	if (mapAttribIDs.Has(TDCA_PROJECTNAME))
		GetDlgItem(IDC_PROJECTNAME)->SetFocus();

	if (mapAttribIDs.Has(TDCA_LOCK))
		UpdateControls(FALSE);
}

void CToDoCtrl::GetAttributesAffectedByMod(TDC_ATTRIBUTE nAttrib, CTDCAttributeMap& mapAttribIDs) const
{
	mapAttribIDs.Add(nAttrib);

	// Check for attribute dependencies
	switch (nAttrib)
	{
	case TDCA_DEPENDENCY:
		if (HasStyle(TDCS_AUTOADJUSTDEPENDENCYDATES))
		{
			mapAttribIDs.Add(TDCA_DUEDATE);
			mapAttribIDs.Add(TDCA_STARTDATE);
		}
		break;

	case TDCA_DUEDATE:
		// If this extension view wants due or start dates and dependents may
		// have changed then we send all tasks with dates
		if (m_taskTree.SelectionHasDependents() &&
			HasStyle(TDCS_AUTOADJUSTDEPENDENCYDATES))
		{
			mapAttribIDs.Add(TDCA_STARTDATE);
		}

		if (HasStyle(TDCS_DUEHAVEHIGHESTPRIORITY) &&
			HasStyle(TDCS_USEHIGHESTPRIORITY))
		{
			mapAttribIDs.Add(TDCA_PRIORITY);
		}

		if (HasStyle(TDCS_SYNCTIMEESTIMATESANDDATES))
		{
			mapAttribIDs.Add(TDCA_STARTDATE);
			mapAttribIDs.Add(TDCA_TIMEEST);
		}
		break;

	case TDCA_STARTDATE:
		if (HasStyle(TDCS_SYNCTIMEESTIMATESANDDATES))
		{
			mapAttribIDs.Add(TDCA_DUEDATE);
			mapAttribIDs.Add(TDCA_TIMEEST);
		}
		break;

	case TDCA_DONEDATE:
		mapAttribIDs.Add(TDCA_SUBTASKDONE);

		if (m_taskTree.SelectionHasDependents() &&
			HasStyle(TDCS_AUTOADJUSTDEPENDENCYDATES))
		{
			mapAttribIDs.Add(TDCA_DUEDATE);
			mapAttribIDs.Add(TDCA_STARTDATE);
		}

		if (m_taskTree.SelectionHasRecurring())
		{
			mapAttribIDs.Add(TDCA_DUEDATE);
			mapAttribIDs.Add(TDCA_STARTDATE);
		}
		
		if (HasStyle(TDCS_AVERAGEPERCENTSUBCOMPLETION) &&
			HasStyle(TDCS_INCLUDEDONEINAVERAGECALC))
		{
			mapAttribIDs.Add(TDCA_PERCENT);
		}

		if (HasStyle(TDCS_INCLUDEDONEINRISKCALC) &&
			HasStyle(TDCS_USEHIGHESTRISK))
		{
			mapAttribIDs.Add(TDCA_RISK);
		}

		if (HasStyle(TDCS_INCLUDEDONEINPRIORITYCALC) &&
			HasStyle(TDCS_USEHIGHESTPRIORITY))
		{
			mapAttribIDs.Add(TDCA_PRIORITY);
		}

		if (!m_sCompletionStatus.IsEmpty())
		{
			mapAttribIDs.Add(TDCA_STATUS);
		}
		break;

	case TDCA_CUSTOMATTRIBDEFS:
		// Special case: We replace the definition 
		// attribute with the value attribute
		mapAttribIDs.Remove(TDCA_CUSTOMATTRIBDEFS);
		mapAttribIDs.Add(TDCA_CUSTOMATTRIB);
		break;

	case TDCA_TIMEEST:
		if (HasStyle(TDCS_AUTOCALCPERCENTDONE))
		{
			mapAttribIDs.Add(TDCA_PERCENT);
		}

		if (HasStyle(TDCS_SYNCTIMEESTIMATESANDDATES))
		{
			mapAttribIDs.Add(TDCA_DUEDATE);
		}
		break;
	}

	// Finally check for colour change
	if (ModsCauseColorChange(mapAttribIDs) && !mapAttribIDs.Has(TDCA_ALL))
		mapAttribIDs.Add(TDCA_COLOR);
}

BOOL CToDoCtrl::ModCausesColorChange(TDC_ATTRIBUTE nModType) const
{
	switch (nModType)
	{
	case TDCA_ALL:
		return TRUE;

	case TDCA_COLOR:
		return !HasStyle(TDCS_COLORTEXTBYPRIORITY) &&
				!HasStyle(TDCS_COLORTEXTBYATTRIBUTE) &&
				!HasStyle(TDCS_COLORTEXTBYNONE);

	case TDCA_CATEGORY:
	case TDCA_ALLOCBY:
	case TDCA_ALLOCTO:
	case TDCA_STATUS:
	case TDCA_VERSION:
	case TDCA_EXTERNALID:
	case TDCA_TAGS:
		return (HasStyle(TDCS_COLORTEXTBYATTRIBUTE) && (m_taskTree.GetColorByAttribute() == nModType));

	case TDCA_DONEDATE:
		return (m_taskTree.GetCompletedTaskColor() != CLR_NONE);

	case TDCA_DUEDATE:
		{
			COLORREF crDue, crDueToday;
			m_taskTree.GetDueTaskColors(crDue, crDueToday);

			return ((crDue != CLR_NONE) || (crDueToday != CLR_NONE));
		}

	case TDCA_PRIORITY:
		return HasStyle(TDCS_COLORTEXTBYPRIORITY);
	}

	// all else
	return FALSE;
}

BOOL CToDoCtrl::ModsCauseColorChange(const CTDCAttributeMap& mapAttrib) const
{
	POSITION pos = mapAttrib.GetStartPosition();

	while (pos)
	{
		TDC_ATTRIBUTE nAttrib = mapAttrib.GetNext(pos);

		if (ModCausesColorChange(nAttrib))
			return TRUE;
	}

	// else
	return FALSE;
}

LRESULT CToDoCtrl::OnCommentsChange(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	UpdateTask(TDCA_COMMENTS);
	return 0L;
}

LRESULT CToDoCtrl::OnCommentsGetTooltip(WPARAM /*wParam*/, LPARAM lParam)
{
	ASSERT(lParam);

	ICCLINKTOOLTIP* pTT = (ICCLINKTOOLTIP*)lParam;
	CString sLink(pTT->szLink);

	if (!sLink.IsEmpty())
	{
		CString sTooltip;
		CString sFile;
		DWORD dwTaskID = 0;

		// Handle Local task links only
		if (ParseTaskLink(sLink, TRUE, dwTaskID, sFile) && dwTaskID && sFile.IsEmpty())
		{
			sTooltip = m_data.GetTaskTitle(dwTaskID);
			ASSERT(!sTooltip.IsEmpty());
		}
		else // forward to parent
		{
			TOOLTIPTEXT tip = { 0 };

			tip.hdr.hwndFrom = GetSafeHwnd();
			tip.hdr.idFrom = GetDlgCtrlID();
			tip.hdr.code = TTN_NEEDTEXT;

			if (GetParent()->SendMessage(WM_TDCM_GETLINKTOOLTIP, (WPARAM)pTT->szLink, (LPARAM)&tip))
			{
				sTooltip = tip.szText;

				if (sTooltip.IsEmpty())
					sTooltip = tip.lpszText;
			}
		}

		if (!sTooltip.IsEmpty())
		{
			lstrcpyn(pTT->szTooltip, sTooltip, ICCLINKTOOLTIPLEN);
			return TRUE;
		}
	}

	return FALSE;
}

LRESULT CToDoCtrl::OnCommentsKillFocus(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	HandleUnsavedComments();
	return 0L;
}

LRESULT CToDoCtrl::OnCommentsDoHelp(WPARAM /*wParam*/, LPARAM lParam)
{
	AfxGetApp()->WinHelp(lParam);
	return 0L;
}

BOOL CToDoCtrl::OnHelpInfo(HELPINFO* /*lpHelpInfo*/)
{
	AfxGetApp()->WinHelp(IDD_TODOCTRL_DIALOG);
	return TRUE;
}

void CToDoCtrl::OnChangeProjectName()
{
	UpdateData();
	SetModified(TDCA_PROJECTNAME);
}

LRESULT CToDoCtrl::OnTreeDragAbort(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	return m_treeDragDrop.ProcessMessage(GetCurrentMessage());
}

LRESULT CToDoCtrl::OnTreeDragEnter(WPARAM /*wParam*/, LPARAM lParam)
{
	ASSERT(!IsReadOnly());

	if (!m_treeDragDrop.ProcessMessage(GetCurrentMessage()))
		return FALSE;

	const DRAGDROPINFO* pDDI = (DRAGDROPINFO*)lParam;

	return IsSelectedTaskMoveEnabled(pDDI->bLeftDrag ? TDCM_LEFTDRAG : TDCM_RIGHTDRAG);
}

LRESULT CToDoCtrl::OnTreeDragPreMove(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	return m_treeDragDrop.ProcessMessage(GetCurrentMessage());
}

LRESULT CToDoCtrl::OnTreeDragOver(WPARAM /*wParam*/, LPARAM lParam)
{
	ASSERT(!IsReadOnly());

	if (m_treeDragDrop.ProcessMessage(GetCurrentMessage()) == DD_DROPEFFECT_NONE)
		return DD_DROPEFFECT_NONE;

	HTREEITEM htiOver, htiAfter;
		
	if (!m_treeDragDrop.GetDropTarget(htiOver, htiAfter, m_bDragDropSubtasksAtTop))
		return DD_DROPEFFECT_NONE;

	const DRAGDROPINFO* pDDI = (DRAGDROPINFO*)lParam;
	DWORD dwTargetID = GetTaskID(htiOver);

	return GetSelectedTasksDropEffect(dwTargetID, pDDI->bLeftDrag);
}

LRESULT CToDoCtrl::OnTreeDragDrop(WPARAM /*wParam*/, LPARAM lParam)
{
	ASSERT(!IsReadOnly());

	if (!m_treeDragDrop.ProcessMessage(GetCurrentMessage()))
		return FALSE;

	HTREEITEM htiDropOn, htiDropAfter;
		
	if (!m_treeDragDrop.GetDropTarget(htiDropOn, htiDropAfter, m_bDragDropSubtasksAtTop))
		return FALSE;

	const DRAGDROPINFO* pDDI = (DRAGDROPINFO*)lParam;
	DWORD dwTargetID = GetTaskID(htiDropOn);

	DD_DROPEFFECT nDrop = GetSelectedTasksDropEffect(dwTargetID, pDDI->bLeftDrag);

	if (nDrop == DD_DROPEFFECT_NONE)
		return FALSE;
			
	// Handle right-click drop context menu
	if (!pDDI->bLeftDrag)
	{
		ASSERT(nDrop == DD_DROPEFFECT_MOVE);

		CEnMenu menu;

		if (menu.LoadMenu(IDR_TREEDRAGDROP, *this, TRUE))
		{
			if (m_treeDragDrop.IsDropOn() && dwTargetID)
				m_taskTree.SelectDropTarget(htiDropOn);

			// Menu items are enabled by default so we just need
			// to disable as required
			CMenu* pSubMenu = menu.GetSubMenu(0);
			UINT nDisabled = (MF_BYCOMMAND | MF_GRAYED);

			if (!CanDropSelectedTasks(DD_DROPEFFECT_COPY, htiDropOn))
				pSubMenu->EnableMenuItem(ID_TDD_COPYTASK, nDisabled);

			if (!CanDropSelectedTasks(DD_DROPEFFECT_MOVE, htiDropOn))
				pSubMenu->EnableMenuItem(ID_TDD_MOVETASK, nDisabled);

			if (!CanDropSelectedTasks(DD_DROPEFFECT_LINK, htiDropOn))
				pSubMenu->EnableMenuItem(ID_TDD_REFTASK, nDisabled);

			// disable dependency and file links if dropping in-between tasks
			// or dropping on locked tasks
			BOOL bTargetIsLocked = m_calculator.IsTaskLocked(dwTargetID);

			if (bTargetIsLocked || !m_treeDragDrop.IsDropOn())
			{
				pSubMenu->EnableMenuItem(ID_TDD_SETTASKDEPENDENCY, nDisabled);
				pSubMenu->EnableMenuItem(ID_TDD_ADDTASKDEPENDENCY, nDisabled);
				pSubMenu->EnableMenuItem(ID_TDD_ADDFILELINK, nDisabled);
			}
			else if (m_taskTree.SelectionHasTask(dwTargetID, TRUE)) // prevent dependency on self
			{
				pSubMenu->EnableMenuItem(ID_TDD_SETTASKDEPENDENCY, nDisabled);
				pSubMenu->EnableMenuItem(ID_TDD_ADDTASKDEPENDENCY, nDisabled);
			}

			// Display the menu, returning the selected command
			CPoint ptCursor(pDDI->pt);
			::ClientToScreen(pDDI->hwndTarget, &ptCursor);

			UINT nCmdID = ::TrackPopupMenu(*pSubMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_LEFTBUTTON,
										   ptCursor.x, ptCursor.y, 0, *this, NULL);

			nDrop = DD_DROPEFFECT_NONE;

			switch (nCmdID)
			{
				case ID_TDD_REFTASK:
					nDrop = DD_DROPEFFECT_LINK;
					break;

				case ID_TDD_COPYTASK:
					nDrop = DD_DROPEFFECT_COPY;
					break;

				case ID_TDD_MOVETASK:
					nDrop = DD_DROPEFFECT_MOVE;
					break;

				case ID_TDD_SETTASKDEPENDENCY:
				case ID_TDD_ADDTASKDEPENDENCY:
					{
						IMPLEMENT_DATA_UNDO_EDIT(m_data);

						CDWordArray aTaskIDs;
						int nTask = GetSelectedTaskIDs(aTaskIDs, TRUE);

						CStringArray aDepends;
						aDepends.SetSize(nTask);

						while (nTask--)
							aDepends[nTask] = TODOITEM::FormatTaskLink(aTaskIDs[nTask], FALSE); // not as URL

						CDWordArray aModTaskIDs;
						aModTaskIDs.Add(dwTargetID);

						m_data.SetTaskDependencies(dwTargetID, aDepends, (nCmdID == ID_TDD_ADDTASKDEPENDENCY));
						SetModified(TDCA_DEPENDENCY, aModTaskIDs);
					}
					break;

				case ID_TDD_ADDFILELINK:
					{
						IMPLEMENT_DATA_UNDO_EDIT(m_data);

						CDWordArray aTaskIDs;
						int nTask = GetSelectedTaskIDs(aTaskIDs, TRUE);

						CStringArray aFileRefs;
						aFileRefs.SetSize(nTask);

						while (nTask--)
							aFileRefs[nTask] = TODOITEM::FormatTaskLink(aTaskIDs[nTask], TRUE); // as URL

						CDWordArray aModTaskIDs;
						aModTaskIDs.Add(dwTargetID);

						m_data.SetTaskFileRefs(dwTargetID, aFileRefs, TRUE);
						SetModified(TDCA_FILEREF, aModTaskIDs);
					}
					break;

				default:
					break;
			}

			m_taskTree.SelectDropTarget(NULL);
			m_taskTree.RedrawColumns();
		}
	}

	return DropSelectedTasks(nDrop, htiDropOn, htiDropAfter);
}

BOOL CToDoCtrl::CanDropSelectedTasks(DD_DROPEFFECT nDrop, HTREEITEM htiDropTarget) const
{
	DWORD dwTargetID = GetTaskID(htiDropTarget);

	if (!IsValidSelectedTaskMoveTarget(dwTargetID, nDrop))
		return FALSE;

	switch (nDrop)
	{
		case DD_DROPEFFECT_COPY:
			return TRUE;

		case DD_DROPEFFECT_LINK:
			return TRUE;

		case DD_DROPEFFECT_MOVE:
			return m_taskTree.IsSelectedTaskMoveEnabled(TDCM_NONDRAG);

		case DD_DROPEFFECT_NONE:
			return FALSE;
	}

	// all else
	ASSERT(0);
	return FALSE;
}

BOOL CToDoCtrl::DropSelectedTasks(DD_DROPEFFECT nDrop, HTREEITEM htiDropTarget, HTREEITEM htiDropAfter)
{
	if (!CanDropSelectedTasks(nDrop, htiDropTarget))
	{
		return FALSE;
	}

	CLockUpdates lu(*this);

	switch (nDrop)
	{
	case DD_DROPEFFECT_COPY:
	case DD_DROPEFFECT_LINK:
		{
			// copy selection as xml
			CTaskFile tasks;
			TDCGETTASKS filter;

			// if pasting references then we don't want all subtasks just 
			// the ones actually selected
			BOOL bDropRefs = (nDrop == DD_DROPEFFECT_LINK);

			if (bDropRefs)
				filter.dwFlags = TDCGSTF_NOTSUBTASKS;

			if (GetSelectedTasks(tasks, filter))
			{
				IMPLEMENT_DATA_UNDO(m_data, TDCUAT_COPY);
				HOLD_REDRAW(*this, m_taskTree);

				// fix up the dependencies of the copied tasks
				if (bDropRefs)
					PrepareTaskIDsForPasteAsRef(tasks);
				else
					PrepareTasksForPaste(tasks, TDCR_YES, TRUE);

				// then add them with impunity!
				PasteTasksToTree(tasks, htiDropTarget, htiDropAfter, TDCR_NO, TRUE);

				// if the parent was marked as done and this task is not
				// then mark the parent as incomplete too
				DWORD dwDestParentID = m_taskTree.GetTaskID(htiDropTarget);
				FixupParentCompletion(dwDestParentID);
			}
		}
		break;

	case DD_DROPEFFECT_MOVE:
		{
			IMPLEMENT_DATA_UNDO(m_data, TDCUAT_MOVE);
			HOLD_REDRAW(*this, m_taskTree);

			DWORD dwSrcParentID = GetSelectedTaskParentID(); // zero for multiple parents
			
			DWORD dwDestParentID = m_taskTree.GetTaskID(htiDropTarget);
			DWORD dwDestPrevSiblingID = m_taskTree.GetTaskID(htiDropAfter);

			CDWordArray aSelTaskIDs;
			DWORD dwUnused = 0;

			m_taskTree.GetSelectedTaskIDs(aSelTaskIDs, dwUnused, TRUE);

			if (m_data.MoveTasks(aSelTaskIDs, dwDestParentID, dwDestPrevSiblingID))
			{
				m_taskTree.MoveSelection(htiDropTarget, htiDropAfter);

				// if the parent was marked as done and this task is not
				// then mark the parent as incomplete too
				FixupParentCompletion(dwDestParentID);

				if (m_taskTree.SelectionHasSameParent() && (dwSrcParentID == dwDestParentID))
					SetModified(TDCA_POSITION_SAMEPARENT);
				else
					SetModified(TDCA_POSITION_DIFFERENTPARENT);
			}
		}
		break;

	case DD_DROPEFFECT_NONE:
	default:
		ASSERT(0);
		return FALSE;
	}

	return TRUE;
}

void CToDoCtrl::FixupParentCompletion(DWORD dwParentID)
{
	VERIFY(m_data.FixupParentCompletion(dwParentID, !m_sCompletionStatus.IsEmpty()));
}

void CToDoCtrl::RemoveNonSelectedTasks(CTaskFile& tasks)
{
	RemoveNonSelectedTasks(tasks, tasks.GetFirstTask());
}
	
void CToDoCtrl::RemoveNonSelectedTasks(CTaskFile& tasks, HTASKITEM hTask)
{
	if (hTask)
	{
		// siblings first
		RemoveNonSelectedTasks(tasks, tasks.GetNextTask(hTask));

		LPCTSTR szSelected = tasks.GetTaskMetaData(hTask, _T("selected"));

		if (!szSelected || (szSelected[0] != '1'))
		{
			tasks.DeleteTask(hTask);
			return;
		}

		// children
		RemoveNonSelectedTasks(tasks, tasks.GetFirstTask(hTask));
	}
}

int CToDoCtrl::GetSelectedTaskIDs(const CTaskFile& tasks, CDWordArray& aTaskIDs)
{
	return GetSelectedTaskIDs(tasks, tasks.GetFirstTask(), aTaskIDs);
}

int CToDoCtrl::GetSelectedTaskIDs(const CTaskFile& tasks, HTASKITEM hTask, CDWordArray& aTaskIDs)
{
	if (hTask)
	{
		LPCTSTR szSelected = tasks.GetTaskMetaData(hTask, _T("selected"));

		if (szSelected && (szSelected[0] == '1'))
			aTaskIDs.Add(tasks.GetTaskID(hTask));

		// siblings
		GetSelectedTaskIDs(tasks, tasks.GetNextTask(hTask), aTaskIDs);

		// children
		GetSelectedTaskIDs(tasks, tasks.GetFirstTask(hTask), aTaskIDs);
	}

	return aTaskIDs.GetSize();
}

void CToDoCtrl::PrepareTasksForPaste(CTaskFile& tasks, TDC_RESETIDS nResetID, BOOL bResetCreation) const
{
	if (nResetID == TDCR_NO || (tasks.GetTaskCount() == 0))
		return; // nothing to do

	// work out what new IDs we need and map them to the existing IDs
	CMapID2ID mapID;
	mapID.InitHashTable(tasks.GetTaskCount());

	DWORD dwNextID(max(tasks.GetNextUniqueID(), m_dwNextUniqueID));
	BuildTaskIDMapForPaste(tasks, tasks.GetFirstTask(NULL), dwNextID, mapID, nResetID, TRUE);

	// then fix them
	PrepareTasksForPaste(tasks, tasks.GetFirstTask(NULL), bResetCreation, mapID, TRUE);
}

void CToDoCtrl::PrepareTaskIDsForPasteAsRef(CTaskFile& tasks) const
{
	if (tasks.GetTaskCount() == 0)
		return; // nothing to do

	CMapID2ID mapID;
	mapID.InitHashTable(tasks.GetTaskCount());

	// work out what new IDs we need and map them to the existing IDs
	DWORD dwNextID(max(tasks.GetNextUniqueID(), m_dwNextUniqueID));
	BuildTaskIDMapForPaste(tasks, tasks.GetFirstTask(NULL), dwNextID, mapID, TDCR_YES, TRUE);

	// work thru the 'new' tasks adding internal ref back to original
	POSITION pos = mapID.GetStartPosition();

	while (pos)
	{
		DWORD dwOrg, dwRef;
		mapID.GetNextAssoc(pos, dwOrg, dwRef);

		HTASKITEM hTask = tasks.FindTask(dwOrg);

		if (hTask)
		{
			// replace org ID with ref ID
			tasks.SetTaskID(hTask, dwRef);

			// don't create references to references
			if (tasks.GetTaskReferenceID(hTask) == 0)
			{
				// link ref task back to org
				tasks.SetTaskReferenceID(hTask, dwOrg);
			}
		}
	}
}

void CToDoCtrl::BuildTaskIDMapForPaste(CTaskFile& tasks, HTASKITEM hTask, DWORD& dwNextID, 
										CMapID2ID& mapID, TDC_RESETIDS nResetID, BOOL bAndSiblings) const
{
	if (nResetID == TDCR_NO) // sanity check
		return;

	if (!hTask)
		return;

	// we map this task only if it needs a new ID
	DWORD dwTaskID = tasks.GetTaskID(hTask);

	if ((dwTaskID <= 0) || (nResetID == TDCR_YES) || m_data.HasTask(dwTaskID))
		mapID[dwTaskID] = dwNextID++;

	// children
	BuildTaskIDMapForPaste(tasks, tasks.GetFirstTask(hTask), dwNextID, mapID, nResetID, TRUE);

	// handle sibling tasks WITHOUT RECURSION
	if (bAndSiblings)
	{
		hTask = tasks.GetNextTask(hTask);
		
		while (hTask)
		{
			// FALSE == don't recurse on siblings
			BuildTaskIDMapForPaste(tasks, hTask, dwNextID, mapID, nResetID, FALSE);
			
			hTask = tasks.GetNextTask(hTask);
		}
	}
}

void CToDoCtrl::PrepareTasksForPaste(CTaskFile& tasks, HTASKITEM hTask, BOOL bResetCreation, const CMapID2ID& mapID, BOOL bAndSiblings) const
{
	if (!hTask)
		return;

	// change task's own ID
	DWORD dwTaskID = tasks.GetTaskID(hTask), dwNewID = 0;

	if (mapID.Lookup(dwTaskID, dwNewID))
	{
		ASSERT (dwNewID); // sanity check
		tasks.SetTaskID(hTask, dwNewID);

		// And give it a new creation date and creator
		if (bResetCreation)
		{
			tasks.SetTaskCreationDate(hTask, COleDateTime::GetCurrentTime());
			tasks.SetTaskCreatedBy(hTask, m_tdiDefault.sCreatedBy);
		}
	}

	// dependencies first
	CStringArray aDepends;
	int nDepend = tasks.GetTaskDependencies(hTask, aDepends);

	BOOL bChanged = FALSE;

	while (nDepend--)
	{
		CString sDepends = aDepends[nDepend];

		if (PrepareTaskLinkForPaste(sDepends, mapID))
		{
			aDepends[nDepend] = sDepends;
			bChanged = TRUE;
		}
	}

	// update taskfile if any dependency was changed
	if (bChanged)
		tasks.SetTaskDependencies(hTask, aDepends);
	
	// file references
	CStringArray aFileRefs;
	int nFile = tasks.GetTaskFileLinks(hTask, aFileRefs);

	bChanged = FALSE;

	while (nFile--)
	{
		CString sFileRef = aFileRefs[nFile];

		if (PrepareTaskLinkForPaste(sFileRef, mapID))
		{
			aFileRefs[nFile] = sFileRef;
			bChanged = TRUE;
		}
	}

	// update taskfile if any file link was changed
	if (bChanged)
		tasks.SetTaskFileLinks(hTask, aFileRefs);

	// children
	PrepareTasksForPaste(tasks, tasks.GetFirstTask(hTask), bResetCreation, mapID, TRUE);

	// siblings
	// handle sibling tasks WITHOUT RECURSION
	if (bAndSiblings)
	{
		hTask = tasks.GetNextTask(hTask);
		
		while (hTask)
		{
			// FALSE == don't recurse on siblings
			PrepareTasksForPaste(tasks, hTask, bResetCreation, mapID, FALSE);
			
			hTask = tasks.GetNextTask(hTask);
		}
	}
}

BOOL CToDoCtrl::PrepareTaskLinkForPaste(CString& sLink, const CMapID2ID& mapID) const
{
	DWORD dwTaskID;
	CString sFile;

	BOOL bURL = TODOITEM::IsTaskLink(sLink, TRUE);

	VERIFY(ParseTaskLink(sLink, bURL, dwTaskID, sFile));
	
	if (!dwTaskID)
		return FALSE;

	// does the ID need changing
	DWORD dwNewID = 0;
	
	if (mapID.Lookup(dwTaskID, dwNewID))
	{
		ASSERT (dwNewID); // sanity check
		
		// make sure the file path matches us
		if (!sFile.IsEmpty() && HasFilePath())
		{
			if (!FileMisc::IsSamePath(sFile, m_sLastSavePath))
				return FALSE;
		}
		
		// update link
		sLink = TODOITEM::FormatTaskLink(dwNewID, bURL, sFile);
		return TRUE;
	}

	return FALSE;

}

BOOL CToDoCtrl::PreTranslateMessage(MSG* pMsg) 
{
	if (m_infoTip.GetSafeHwnd())
		m_infoTip.FilterToolTipMessage(pMsg);

	if (m_ctrlComments.ProcessMessage(pMsg))
		return TRUE;

	if (m_treeDragDrop.ProcessMessage(pMsg))
		return TRUE;
	
	if (m_taskTree.PreTranslateMessage(pMsg))
		return TRUE;
	
	switch (pMsg->message)
	{
	case  WM_CHAR:
		// eat any enter keys destined for non-multiline
		// edits else they'll beep
		if (pMsg->wParam == VK_RETURN && CWinClasses::IsEditControl(pMsg->hwnd))
		{
			DWORD dwStyle = ::GetWindowLong(pMsg->hwnd, GWL_STYLE);

			if (!(dwStyle & ES_MULTILINE))
				return TRUE;
		}
		break;

	case WM_KEYDOWN:
		{
			// handle <return> and <escape> for tree label editing
			if (pMsg->hwnd == (HWND)m_taskTree.Tree().SendMessage(TVM_GETEDITCONTROL))
			{
				// this should no longer need handling
				ASSERT(0);
			}
			// handle <enter> and TDCS_FOCUSTREEONENTER
			else if (HasStyle(TDCS_FOCUSTREEONENTER) && (pMsg->wParam == VK_RETURN))
			{
				// have to be careful here because dropped-down comboboxes should
				// take precedence. so we have to check explicitly for them
				// and make sure they're not in a dropped state
				CWnd* pFocus = GetFocus();
				
				if (pFocus && IsChild(pFocus))
				{
					if (!CtrlWantsEnter(*pFocus))
						SetFocusToTasks();

					return FALSE; // allow further routing
				}
			}
		}
		break;
	}
	
	return CRuntimeDlg::PreTranslateMessage(pMsg);
}

BOOL CToDoCtrl::MoveSelectedTask(TDC_MOVETASK nDirection)
{
	if (!CanMoveSelectedTask(nDirection))
		return FALSE;

	Flush(); // end any editing action
	SetFocusToTasks(); // else datetime controls get their focus screwed

	IMPLEMENT_DATA_UNDO(m_data, TDCUAT_MOVE);

	DWORD dwDestParentID = 0, dwDestPrevSiblingID = 0;
	VERIFY(m_taskTree.GetInsertLocation(nDirection, dwDestParentID, dwDestPrevSiblingID));

	// Get selected tasks without duplicate subtasks
	// because the subtasks will be moved with their parents
	CDWordArray aSelTaskIDs;
	DWORD dwUnused;
	m_taskTree.GetSelectedTaskIDs(aSelTaskIDs, dwUnused, TRUE);

	// Move the associated tree items
	CLockUpdates lu(*this);
	HOLD_REDRAW(*this, m_taskTree);

	if (!m_taskTree.MoveSelection(nDirection))
	{
		ASSERT(0);
		return FALSE;
	}

	// move the tasks
	if (!m_data.MoveTasks(aSelTaskIDs, dwDestParentID, dwDestPrevSiblingID))
	{
		ASSERT(0);
		return FALSE;
	}

	// refresh parent states if moving to the right (adding subtasks)
	if (nDirection == TDCM_RIGHT)
		FixupParentCompletion(dwDestParentID);

	if ((nDirection == TDCM_DOWN) || (nDirection == TDCM_UP))
		SetModified(TDCA_POSITION_SAMEPARENT);
	else
		SetModified(TDCA_POSITION_DIFFERENTPARENT);

	return TRUE;
}

BOOL CToDoCtrl::GotoNextTopLevelTask(TDC_GOTO nDirection)
{
	HTREEITEM htiNext = TCH().GetNextTopLevelItem(GetSelectedItem(), nDirection == TDCG_NEXT);
	
	if (htiNext)
		SelectItem(htiNext);

	return (htiNext != NULL);
}

BOOL CToDoCtrl::CanGotoNextTopLevelTask(TDC_GOTO nDirection) const
{
	return (NULL != TCH().GetNextTopLevelItem(GetSelectedItem(), nDirection == TDCG_NEXT));
}

BOOL CToDoCtrl::GotoNextTask(TDC_GOTO nDirection)
{
	HTREEITEM htiNext = NULL;

	if (nDirection == TDCG_NEXT)
		htiNext = TCH().GetNextVisibleItem(GetSelectedItem());
	else
		htiNext = TCH().GetPrevVisibleItem(GetSelectedItem());
	
	if (htiNext)
		SelectItem(htiNext);

	return (htiNext != NULL);
}

BOOL CToDoCtrl::CanGotoNextTask(TDC_GOTO nDirection) const
{
	HTREEITEM htiNext = NULL;

	if (nDirection == TDCG_NEXT)
		htiNext = TCH().GetNextVisibleItem(GetSelectedItem());
	else
		htiNext = TCH().GetPrevVisibleItem(GetSelectedItem());

	return (htiNext != NULL);
}

void CToDoCtrl::SetDueTaskColors(COLORREF crDue, COLORREF crDueToday)
{
	m_taskTree.SetDueTaskColors(crDue, crDueToday);
}

void CToDoCtrl::SetPriorityColors(const CDWordArray& aColors) 
{ 
	if (m_taskTree.SetPriorityColors(aColors))
		m_cbPriority.SetColors(aColors);
}

void CToDoCtrl::SetProjectName(const CString& sProjectName)
{
	if (m_sProjectName != sProjectName)
	{
		m_sProjectName = sProjectName;

		if (GetSafeHwnd())
			UpdateDataEx(this, IDC_PROJECTNAME, m_sProjectName, FALSE);
	}
}

BOOL CToDoCtrl::GetArchivePath(CString& sArchivePath) const
{
	// can't archive archives
	if (!m_bArchive && HasFilePath())
	{
		sArchivePath = m_sLastSavePath;
		return FileMisc::AddToFileName(sArchivePath, ARCHIVE_ID);
	}

	// else
	sArchivePath.Empty();
	return FALSE;
}

CString CToDoCtrl::GetFriendlyProjectName(int nUntitledIndex) const
{
	CString sProjectName(m_sProjectName);
	sProjectName.TrimRight();
	
	if (sProjectName.IsEmpty())
	{
		if (HasFilePath())
		{
			sProjectName = FileMisc::GetFileNameFromPath(m_sLastSavePath, FALSE);
		}
		else
		{
			sProjectName = CEnString(IDS_TDC_UNTITLEDFILE);

			if (nUntitledIndex > 0)
				sProjectName += Misc::Format(nUntitledIndex);
		}
	}
	
	return sProjectName;
}

void CToDoCtrl::SetFilePath(const CString& sPath) 
{ 
	m_sLastSavePath = sPath; 
	m_ctrlComments.SetPreferencesFilePath(sPath);
	
	CString sFolder(FileMisc::GetFolderFromFilePath(sPath));
	m_taskTree.SetTasklistFolder(sFolder);
	m_cbFileRef.SetCurrentFolder(sFolder);
}

CString CToDoCtrl::GetStylesheetPath() const
{
	if (!m_sXslHeader.IsEmpty())
	{
		CStringArray aLinks, aText;

		if (WebMisc::ExtractHtmlLinks(m_sXslHeader, aLinks, aText))
		{
			// look for .xsl
			int nLink = aLinks.GetSize();

			while (nLink--)
			{
				CString sFile(aLinks[nLink]);

				if (FileMisc::GetExtension(Misc::ToUpper(sFile)) == _T(".XSL"))
				{
					if (HasFilePath())
						FileMisc::MakeFullPath(sFile, FileMisc::GetFolderFromFilePath(m_sLastSavePath));

					return sFile;
				}
			}
		}
	}

	// not found
	return _T("");
}

void CToDoCtrl::SetCommentsSize(int nSize)
{
	if (nSize != m_nCommentsSize)
	{
		m_nCommentsSize = nSize;
		Resize();
	}
}

void CToDoCtrl::OnContextMenu(CWnd* pWnd, CPoint point) 
{
	Flush();

	CRuntimeDlg::OnContextMenu(pWnd, point);
}

TDC_HITTEST CToDoCtrl::HitTest(const CPoint& ptScreen) const
{
	return m_taskTree.HitTest(ptScreen);
}

DWORD CToDoCtrl::HitTestTask(const CPoint& ptScreen, BOOL bTitleColumnOnly) const
{
	return m_taskTree.HitTestTask(ptScreen, bTitleColumnOnly);
}

TDC_COLUMN CToDoCtrl::HitTestColumn(const CPoint& ptScreen) const
{
	return m_taskTree.HitTestColumn(ptScreen);
}

LRESULT CToDoCtrl::OnAutoComboAddDelete(WPARAM wp, LPARAM /*lp*/)
{
	UINT nCtrlID = LOWORD(wp);

	switch (nCtrlID)
	{
	case IDC_FILEPATH:
		// Note: parent doesn't need to know about this
		return 0L;
		
	case IDC_CATEGORY: 
	case IDC_TAGS:
	case IDC_STATUS:
	case IDC_ALLOCTO:
	case IDC_ALLOCBY:
	case IDC_VERSION:
		{
			TDC_ATTRIBUTE nAttribID = TDC::MapCtrlIDToAttribute(nCtrlID);

			UpdateAutoListData(nAttribID);
			GetParent()->SendMessage(WM_TDCN_LISTCHANGE, 0, nAttribID);
		}		
		break;

	default:
		if (CTDCCustomAttributeUIHelper::IsCustomEditControl(nCtrlID))
		{
			int nCtrl = m_aCustomControls.Find(nCtrlID);

			if (nCtrl != -1)
			{
				GetParent()->SendMessage(WM_TDCN_LISTCHANGE, 0, m_aCustomControls[nCtrl].nAttrib);
				break;
			}
		}
		// all else
		ASSERT(0);
		return 0L;
	}

	// mark ourselves as modified because we now save the lists to the taskfile
	SetModified(TRUE);

	return 0L;
}

void CToDoCtrl::OnAutoComboListChange(TDC_ATTRIBUTE nAttribID, CAutoComboBox& combo, CStringArray& aItems)
{
	combo.GetItems(aItems);
	GetParent()->SendMessage(WM_TDCN_LISTCHANGE, 0, nAttribID);
}

void CToDoCtrl::OnSelChangeAllocTo()
{
	UpdateTask(TDCA_ALLOCTO);
}

void CToDoCtrl::OnSelChangeAllocBy()
{
	UpdateTask(TDCA_ALLOCBY);
}

void CToDoCtrl::OnSelChangeStatus()
{
	UpdateTask(TDCA_STATUS);
}

void CToDoCtrl::OnSelChangeVersion()
{
	UpdateTask(TDCA_VERSION);
}

void CToDoCtrl::OnSelChangeCategory()
{
	UpdateTask(TDCA_CATEGORY);
}

void CToDoCtrl::OnSelChangeFileRefPath()
{
	UpdateTask(TDCA_FILEREF);
}

void CToDoCtrl::OnCancelChangeFileRefPath()
{
	// restore combo state
	m_cbFileRef.SetFileList(m_aFileRefs);
}

void CToDoCtrl::OnSelChangeTag()
{
	UpdateTask(TDCA_TAGS);
}

void CToDoCtrl::OnSelCancelAllocTo()
{
	// restore previous combo state
	CStringArray aMatched, aMixed;
	
	m_taskTree.GetSelectedTaskAllocTo(aMatched, aMixed);
	m_cbAllocTo.SetChecked(aMatched, aMixed);
}

void CToDoCtrl::OnSelCancelCategory()
{
	// restore previous combo state
	CStringArray aMatched, aMixed;
	
	m_taskTree.GetSelectedTaskCategories(aMatched, aMixed);
	m_cbCategory.SetChecked(aMatched, aMixed);
}

void CToDoCtrl::OnSelCancelTag()
{
	// restore previous combo state
	CStringArray aMatched, aMixed;
	
	m_taskTree.GetSelectedTaskTags(aMatched, aMixed);
	m_cbTags.SetChecked(aMatched, aMixed);
}

void CToDoCtrl::OnSelCancelAllocBy()
{
	m_cbAllocBy.SelectString(0, GetSelectedTaskAllocBy());
}

void CToDoCtrl::OnSelCancelPriority()
{
	m_cbPriority.SetSelectedPriority(GetSelectedTaskPriority());
}

void CToDoCtrl::OnSelCancelRisk()
{
	m_cbRisk.SetSelectedRisk(GetSelectedTaskRisk());
}

void CToDoCtrl::OnSelCancelStatus()
{
	m_cbStatus.SelectString(0, GetSelectedTaskStatus());
}

void CToDoCtrl::OnSelCancelVersion()
{
	m_cbVersion.SelectString(0, GetSelectedTaskVersion());
}

void CToDoCtrl::OnChangeTimeEstimate()
{
	UpdateTask(TDCA_TIMEEST); 
}

void CToDoCtrl::OnChangeTimeSpent()
{
	// ignore this if it came from time tracking
	if (IsSelectedTaskBeingTimeTracked())
	{
		ASSERT(m_eTimeSpent.GetStyle() & ES_READONLY);
		ASSERT(GetSelectedCount() == 1);
		
		return;
	}

	UpdateTask(TDCA_TIMESPENT); 
}

void CToDoCtrl::OnChangeExternalID()
{
	UpdateTask(TDCA_EXTERNALID); 
}

void CToDoCtrl::OnChangeCost()
{
	UpdateTask(TDCA_COST); 
}

void CToDoCtrl::OnChangeDependency()
{
	UpdateTask(TDCA_DEPENDENCY); 
}

void CToDoCtrl::OnChangeRecurrence()
{
	UpdateTask(TDCA_RECURRENCE); 
}

void CToDoCtrl::OnChangePercent()
{
	// don't handle if this was an 'auto' change
	if (!HasStyle(TDCS_AUTOCALCPERCENTDONE))
		UpdateTask(TDCA_PERCENT);
}

BOOL CToDoCtrl::IsClipboardEmpty(BOOL bCheckID) const
{
	if (CTaskClipboard::IsEmpty())
	{
		return TRUE;
	}
	else if (bCheckID)
	{
		return !CTaskClipboard::ClipIDMatches(GetClipboardID());
	}

	//else
	return FALSE;
}

BOOL CToDoCtrl::CanCopyTaskColumnValues(TDC_COLUMN nColID, BOOL bSelectedTasksOnly) const
{
	return m_taskTree.CanCopyTaskColumnValues(nColID, bSelectedTasksOnly);
}

BOOL CToDoCtrl::CopyTaskColumnValues(TDC_COLUMN nColID, BOOL bSelectedTasksOnly) const
{
	CStringArray aValues;

	if (!CopyTaskColumnValues(nColID, bSelectedTasksOnly, aValues))
		return FALSE;

	// else
	return CClipboard(GetSafeHwnd()).SetText(Misc::FormatArray(aValues, '\n', TRUE));
}

int CToDoCtrl::CopyTaskColumnValues(TDC_COLUMN nColID, BOOL bSelectedTasksOnly, CStringArray& aValues) const
{
	return m_taskTree.CopyTaskColumnValues(nColID, bSelectedTasksOnly, aValues);
}

BOOL CToDoCtrl::CopySelectedTasks() const
{
	if (!GetSelectedCount())
		return FALSE;
	
	ClearCopiedItem();

	// copy selected tasks to clipboard
	TDCGETTASKS filter;
	CTaskFile tasks;

	if (!GetSelectedTasks(tasks, filter))
		return FALSE;
	
	// and their titles
	CHTIList selection;
	CStringArray aTitles;

	VERIFY(TSH().CopySelection(selection, FALSE, TRUE));
	VERIFY(TSH().GetItemTitles(selection, aTitles));

	return CTaskClipboard::SetTasks(tasks, GetClipboardID(), Misc::FormatArray(aTitles, '\n'));
}

BOOL CToDoCtrl::CopySelectedTask() const
{
	return CopySelectedTasks();
}

BOOL CToDoCtrl::CutSelectedTask()
{
	if (CanEditSelectedTask(TDCA_DELETE) && CopySelectedTasks())
	{
		IMPLEMENT_DATA_UNDO(m_data, TDCUAT_DELETE);
			
		DeleteSelectedTask(FALSE, TRUE);
		return TRUE;
	}
	
	return FALSE;
}

BOOL CToDoCtrl::CanPasteTasks(TDC_PASTE nWhere, BOOL bAsRef) const
{
	if (!CanEditSelectedTask(TDCA_PASTE) || IsClipboardEmpty())
		return FALSE;

	// cannot paste references from other tasklists
	if (bAsRef && IsClipboardEmpty(TRUE))
		return FALSE;

	switch (nWhere)
	{
	case TDCP_ONSELTASK:
		// there must be exactly one task selected
		if (GetSelectedCount() != 1)
		{
			return FALSE;
		}
		// can only paste non-references on other non-references
		else if (!bAsRef && IsSelectedTaskReference())
		{
			return FALSE;
		}
		break;
		
	case TDCP_BELOWSELTASK:
		if (!GetSelectedCount())
		{
			return FALSE;
		}
		else
		{
			// parent must be acceptable
			DWORD dwParentID = GetSelectedTaskParentID();

			if (GetSelectedCount() == 1)
			{
				if (dwParentID) // non-root
				{
					if (!bAsRef && m_data.IsTaskReference(dwParentID))
						return FALSE;
				}
				// else => root => okay
			}
			else if (dwParentID) // same parent
			{
				if (!bAsRef && m_data.IsTaskReference(dwParentID))
					return FALSE;
			}
			else // multiple parents
			{
				return FALSE;
			}
		}
		break;
		
	case TDCP_ATBOTTOM:
		break; // always
	}

	return TRUE;
}

void CToDoCtrl::ClearCopiedItem() const
{
	CTaskClipboard::Reset();
}

BOOL CToDoCtrl::PasteTasks(TDC_PASTE nWhere, BOOL bAsRef)
{
	if (!CanPasteTasks(nWhere, bAsRef))
	{
		ASSERT(0);
		return FALSE;
	}

	CString sClipID = GetClipboardID(), sArchiveID;
	BOOL bCheckArchive = GetClipboardID(sArchiveID, TRUE);

	CTaskFile tasks;

	if (!CTaskClipboard::GetTasks(tasks, sClipID))
		return FALSE;

	// else
	HTREEITEM htiDest = NULL, htiDestAfter = NULL;
	
	switch (nWhere)
	{
	case TDCP_ONSELTASK:
		htiDest = m_taskTree.GetTreeSelectedItem();
		htiDestAfter = TVI_FIRST;
		break;
		
	case TDCP_BELOWSELTASK:
		htiDestAfter = m_taskTree.GetTreeSelectedItem();
		
		if (!htiDestAfter)
			htiDestAfter = TVI_LAST;
		else
			htiDest = m_taskTree.GetParentItem(htiDestAfter);
		break;
		
	case TDCP_ATBOTTOM:
		htiDestAfter = TVI_LAST;
		break;
	}
	
	if (bAsRef)
	{
		// remove tasks not originally selected
		RemoveNonSelectedTasks(tasks);
		
		// pre-process the tasks to add themselves
		// as a reference, and then to clear the task ID
		// so that it gets a newly allocated one
		PrepareTaskIDsForPasteAsRef(tasks);
	}
	else
	{
		// pre-process task IDs if the tasks did *not* originate 
		// from us (or our archive) and we're not empty
		TDC_RESETIDS nResetID = TDCR_YES;
		
		if (CTaskClipboard::ClipIDMatches(sClipID) ||
			(bCheckArchive && CTaskClipboard::ClipIDMatches(sArchiveID)))
		{
			nResetID = TDCR_CHECK;
		}
		else if (GetTaskCount() == 0)
		{
			nResetID = TDCR_NO;
		}
		
		// and fix up the dependencies of the tasks
		// and the creation date
		PrepareTasksForPaste(tasks, nResetID, TRUE);
	}
		
	// Merge in any custom attributes we don't already have
	CTDCCustomAttribDefinitionArray aOrgAttribDefs, aPasteAttribDefs;
	aOrgAttribDefs.Copy(m_aCustomAttribDefs);
	
	BOOL bRebuildCustomUI = (tasks.GetCustomAttributeDefs(aPasteAttribDefs) &&
								m_aCustomAttribDefs.Append(aPasteAttribDefs));
	
	IMPLEMENT_DATA_UNDO(m_data, TDCUAT_PASTE);
	{
		HOLD_REDRAW(*this, m_taskTree);
			
		// no need to re-check IDs as we've already done it
		if (!PasteTasksToTree(tasks, htiDest, htiDestAfter, TDCR_NO, TRUE))
		{
			if (bRebuildCustomUI)
			{
				m_aCustomAttribDefs.Copy(aOrgAttribDefs);
			}
			
			return FALSE;
		}
	}

	FixupParentCompletion(GetTaskID(htiDest));
	
	if (bRebuildCustomUI)
	{
		RebuildCustomAttributeUI();
		
		CTDCCustomAttributeDataMap mapData;
		
		if (GetSelectedTaskCustomAttributeData(mapData))
			CTDCCustomAttributeUIHelper::UpdateControls(this, m_aCustomControls, m_aCustomAttribDefs, mapData);
	}
	
	return TRUE;
}

BOOL CToDoCtrl::PasteTasksToTree(const CTaskFile& tasks, HTREEITEM htiDestParent, HTREEITEM htiDestAfter, 
							   TDC_RESETIDS nResetIDs, BOOL bSelectAll)
{
	if (!htiDestParent)
		htiDestParent = TVI_ROOT;
	
	HTASKITEM hTask = tasks.GetFirstTask();
	
	if (!hTask)
		return FALSE;
	
	// cache and clear current selection
	DWORD dwSelID = GetTaskID(TSH().GetFirstItem());

	TCH().SelectItem(NULL);
	TSH().RemoveAll();

	while (hTask)
	{
		htiDestAfter = PasteTaskToTree(tasks, hTask, htiDestParent, htiDestAfter, nResetIDs, TRUE);

		// next task
		hTask = tasks.GetNextTask(hTask);
	}

	m_taskTree.ExpandItem(htiDestParent);
	
	// restore selection
	CDWordArray aTaskIDs;

	if (bSelectAll && GetSelectedTaskIDs(tasks, aTaskIDs))
	{
		m_taskTree.SelectTasks(aTaskIDs);
	}
	else if (dwSelID)
	{
		SelectTask(dwSelID);
	}
	else // select first task
	{
		SelectItem(m_taskTree.GetChildItem());
	}
	
	SetModified(TDCA_PASTE);
	UpdateControls();
	
	return TRUE;
}

void CToDoCtrl::OnTreeClick(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
		
	// special case
	if (Misc::IsKeyPressed(VK_MENU)) // alt key
	{
		NMITEMACTIVATE* pNMIA = (NMITEMACTIVATE*)pNMHDR;

		TDC_COLUMN nColID = (TDC_COLUMN)pNMIA->iSubItem;
		UINT nCtrlID = MapColumnToCtrlID(nColID);
		
		if (nCtrlID)
		{
			// make sure the edit controls are visible
			if (m_nMaxState != TDCMS_NORMAL)
				SetMaximizeState(TDCMS_NORMAL);
			
			GetDlgItem(nCtrlID)->SetFocus();
		}
	}
}

LRESULT CToDoCtrl::OnTDCColumnEditClick(WPARAM wParam, LPARAM lParam)
{
	TDC_COLUMN nColID = (TDC_COLUMN)wParam;
	DWORD dwTaskID = lParam;

	switch (nColID)
	{
	case TDCC_CLIENT:
		ASSERT(CanEditSelectedTask(TDCA_TASKNAME, dwTaskID));
		EditSelectedTaskTitle(FALSE);
		break;
		
	case TDCC_DONE:
		ASSERT(CanEditSelectedTask(TDCA_DONEDATE, dwTaskID));
		SetSelectedTaskDone(!m_data.IsTaskDone(dwTaskID));
		break;
		
	case TDCC_TRACKTIME:
		ASSERT(CanEditSelectedTask(TDCA_TIMESPENT, dwTaskID));
		{
			HTREEITEM hti = m_taskTree.GetTreeSelectedItem();

			ASSERT ((GetSelectedCount() == 1) && 
					IsItemSelected(hti) && 
					m_timeTracking.CanTrackTask(dwTaskID));
			
			ToggleTimeTracking(hti);
		}
		break;
		
	case TDCC_FLAG:
		ASSERT(CanEditSelectedTask(TDCA_FLAG, dwTaskID));
		SetSelectedTaskFlag(!m_data.IsTaskFlagged(dwTaskID));
		break;
		
	case TDCC_LOCK:
		ASSERT(CanEditSelectedTask(TDCA_LOCK, dwTaskID));
		SetSelectedTaskLock(!m_data.IsTaskLocked(dwTaskID));
		break;

	case TDCC_ICON:
		ASSERT(CanEditSelectedTask(TDCA_ICON, dwTaskID));

		// Cancel any drag started by clicking on the tree item icon
		m_treeDragDrop.CancelDrag();

		EditSelectedTaskIcon();
		break;
		
	case TDCC_FILEREF:
		GotoFile(m_data.GetTaskFileRef(dwTaskID, 0));
		break;
		
	case TDCC_DEPENDENCY:
		{
			CDWordArray aLocalDepends;
			CStringArray aOtherDepends;

			if (GetAllSelectedTaskDependencies(aLocalDepends, aOtherDepends))
			{
				if (aLocalDepends.GetSize() == 0)
				{
					if (aOtherDepends.GetSize() > 0)
						ShowTaskLink(aOtherDepends[0], FALSE);
				}
				else
				{
					SelectTasks(aLocalDepends, TRUE);
				}
			}
		}
		break;
		
	case TDCC_RECURRENCE:
		ASSERT(CanEditSelectedTask(TDCA_RECURRENCE, dwTaskID));
		m_eRecurrence.DoEdit();
		break;
		
	case TDCC_REMINDER:
		AfxGetMainWnd()->SendMessage(WM_TDCN_CLICKREMINDERCOL);
		break;
		
	default: // try custom columns
		HandleCustomColumnClick(nColID);
		break;
	}

	return 0L;
}

int CToDoCtrl::GetAllSelectedTaskDependencies(CDWordArray& aLocalDepends, CStringArray& aOtherDepends) const
{
	CStringSet aOther;
	CDWordSet aLocal;
	CDWordArray aTaskIDs;

	int nID = GetSelectedTaskIDs(aTaskIDs, TRUE);

	while (nID--)
	{
		CStringArray aDepends;
		int nDepend = m_data.GetTaskDependencies(aTaskIDs[nID], aDepends);

		while (nDepend--)
		{
			if (!aDepends[nDepend].IsEmpty())
			{
				DWORD dwDependID = _ttoi(aDepends[nDepend]);

				if (dwDependID)
					aLocal.Add(dwDependID);
				else
					aOther.Add(aDepends[nDepend]);
			}
		}
	}

	aOther.CopyTo(aOtherDepends);
	aLocal.CopyTo(aLocalDepends);

	return (aLocalDepends.GetSize() + aOtherDepends.GetSize());
}

UINT CToDoCtrl::MapColumnToCtrlID(TDC_COLUMN nColID) const
{
	TDC_ATTRIBUTE nAttrib = TDC::MapColumnToAttribute(nColID);
	ASSERT(nAttrib != TDCA_NONE);

	int nCtrl;
	for (nCtrl = 0; nCtrl < NUM_CTRLITEMS; nCtrl++)
	{
		const CTRLITEM& ctrl = CTRLITEMS[nCtrl];

		if (ctrl.nAttrib == nAttrib)
			return ctrl.nCtrlID;
	}

	for (nCtrl = 0; nCtrl < m_aCustomControls.GetSize(); nCtrl++)
	{
		const CTRLITEM& ctrl = m_aCustomControls[nCtrl];

		if (ctrl.nAttrib == nAttrib)
			return ctrl.nCtrlID;
	}

	// pick up any stragglers
	switch (nColID)
	{
	case TDCC_DONE:
		return IDC_DONEDATE;

	case TDCC_TRACKTIME:
		return IDC_TIMESPENT;
	}

	ASSERT(0);
	return 0L;
}

TDC_ATTRIBUTE CToDoCtrl::MapCtrlIDToAttribute(UINT nCtrlID) const
{
	if (nCtrlID == 0)
		return TDCA_NONE;

	int nCtrl;
	for (nCtrl = 0; nCtrl < NUM_CTRLITEMS; nCtrl++)
	{
		const CTRLITEM& ctrl = CTRLITEMS[nCtrl];

		if (ctrl.nCtrlID == nCtrlID)
			return ctrl.nAttrib;
	}

	for (nCtrl = 0; nCtrl < m_aCustomControls.GetSize(); nCtrl++)
	{
		const CUSTOMATTRIBCTRLITEM& ctrl = m_aCustomControls[nCtrl];

		if ((ctrl.nCtrlID == nCtrlID) || (ctrl.nBuddyCtrlID == nCtrlID))
			return ctrl.nAttrib;
	}

	// Not everything is an attribute field
	return TDCA_NONE;
}

BOOL CToDoCtrl::HandleCustomColumnClick(TDC_COLUMN nColID)
{
	if (!TDCCUSTOMATTRIBUTEDEFINITION::IsCustomColumn(nColID))
		return FALSE;

	TDCCUSTOMATTRIBUTEDEFINITION attribDef;
	VERIFY (m_aCustomAttribDefs.GetAttributeDef(nColID, attribDef));
	
	TDCCADATA data;
	GetSelectedTaskCustomAttributeData(attribDef.sUniqueID, data);
	
	switch (attribDef.GetDataType())
	{
	case TDCCA_BOOL:
		// toggle the flag state
		if (data.AsBool())
		{
			data.Set(false);
		}
		else
		{
			TCHAR nChar = 0;

			if (attribDef.aDefaultListData.GetSize())
				nChar = attribDef.aDefaultListData[0][0];
		
			data.Set(true, nChar);
		}
		SetSelectedTaskCustomAttributeData(attribDef.sUniqueID, data.AsString(), FALSE);
		return TRUE; // handled
		
	case TDCCA_ICON:
		switch (attribDef.GetListType())
		{
		case TDCCA_FIXEDLIST:
			{
				CString sImage, sDummy;
				BOOL bNext = (!Misc::IsKeyPressed(VK_SHIFT));
				CString sTag = attribDef.GetNextListItem(data.AsString(), bNext);
				
				if (sTag.IsEmpty() || attribDef.DecodeImageTag(sTag, sImage, sDummy))
				{
					SetSelectedTaskCustomAttributeData(attribDef.sUniqueID, sImage, FALSE);
					return TRUE; // handled
				}
			}
			break;

		case TDCCA_FIXEDMULTILIST:
			// do nothing
			break;

		case TDCCA_NOTALIST:
			{
				CTDLTaskIconDlg dialog(m_ilTaskIcons, data.AsString(), TRUE, this);
				
				if (dialog.DoModal() == IDOK)
				{
					SetSelectedTaskCustomAttributeData(attribDef.sUniqueID, dialog.GetIconName(), FALSE);
					return TRUE; // handled
				}
			}
			break;
		}
		break;
		
	default:
		// do item cycling for fixed lists unless they support calculation
		if (attribDef.GetListType() == TDCCA_FIXEDLIST)
		{
			BOOL bNext = (!Misc::IsKeyPressed(VK_SHIFT));
			CString sItem = attribDef.GetNextListItem(data.AsString(), bNext);
			
			SetSelectedTaskCustomAttributeData(attribDef.sUniqueID, sItem, FALSE);
			return TRUE; // handled
		}
		break;
	}

	return FALSE; // not handled
}

void CToDoCtrl::ToggleTimeTracking(HTREEITEM hti)
{
	ASSERT (GetSelectedCount() == 1); // sanity check
	
	DWORD dwTaskID = GetTrueTaskID(hti);

	if (m_timeTracking.IsTrackingTask(dwTaskID, FALSE))
		EndTimeTracking(TRUE, TRUE); 
	else
		BeginTimeTracking(dwTaskID, TRUE);
}

// External
BOOL CToDoCtrl::BeginTimeTracking(DWORD dwTaskID)
{
	return BeginTimeTracking(dwTaskID, FALSE); // don't notify
}

// Internal
BOOL CToDoCtrl::BeginTimeTracking(DWORD dwTaskID, BOOL bNotify)
{
	if (!m_timeTracking.CanTrackTask(dwTaskID))
		return FALSE;

	// if there's a current task being tracked then end it
	EndTimeTracking(TRUE, bNotify);

	// Select the task if this is a requirement
	if (HasStyle(TDCS_TRACKSELECTEDTASKONLY) && !m_taskTree.IsTaskSelected(dwTaskID, TRUE))
		VERIFY(SelectTask(dwTaskID));

	VERIFY(m_timeTracking.BeginTracking(dwTaskID));

	// if the task's start date has not been set then set it now
	if (!m_data.TaskHasDate(dwTaskID, TDCD_START))
		m_data.SetTaskDate(dwTaskID, TDCD_STARTDATE, COleDateTime::GetCurrentTime());

	m_taskTree.SetTimeTrackTaskID(dwTaskID);

	// Update Time spent control
	m_eTimeSpent.CheckButton(ID_TIME_TRACK, TRUE);
	m_eTimeSpent.EnableButton(ID_TIME_TRACK, TRUE);
	m_eTimeSpent.EnableButton(ID_ADD_TIME, FALSE);

	SetCtrlState(m_eTimeSpent, RTCS_READONLY);
	SetTimer(TIMER_TRACK, TIMETRACKPERIOD, NULL);

	// notify parent
	if (bNotify)
		GetParent()->SendMessage(WM_TDCN_TIMETRACK, (WPARAM)GetSafeHwnd(), dwTaskID);

	return TRUE;
}

void CToDoCtrl::SetTimeTrackingReminderInterval(int nMinutes)
{
	m_timeTracking.SetTrackingReminderInterval(nMinutes);
}

// External
void CToDoCtrl::EndTimeTracking(BOOL bAllowConfirm)
{
	EndTimeTracking(bAllowConfirm, FALSE); // don't notify
}

// Internal
void CToDoCtrl::EndTimeTracking(BOOL bAllowConfirm, BOOL bNotify)
{
	// if there's a current task being tracked then log it
	DWORD dwTaskID = m_timeTracking.GetTrackedTaskID(FALSE);

	if (!dwTaskID)
		return;

	KillTimer(TIMER_TRACK);

	// add whatever time is still waiting
	IncrementTrackedTime(TRUE);

	VERIFY(m_timeTracking.EndTracking());

	// prevent re-entrancy
	double dTime = m_dTrackedTimeElapsedHours;
	m_dTrackedTimeElapsedHours = 0.0;

	// log it
	if (HasStyle(TDCS_LOGTIMETRACKING))
	{
		BOOL bShowDialog = (bAllowConfirm && HasStyle(TDCS_DISPLAYLOGCONFIRM));

		DoAddTimeToLogFile(dwTaskID, dTime, bShowDialog);
	}

	// update tree
	m_taskTree.SetTimeTrackTaskID(0);

	// Update Time spent control
	BOOL bCanTimeTrack = m_timeTracking.CanTrackTask(dwTaskID);

	m_eTimeSpent.CheckButton(ID_TIME_TRACK, FALSE);
	m_eTimeSpent.EnableButton(ID_TIME_TRACK, bCanTimeTrack);
	m_eTimeSpent.EnableButton(ID_ADD_TIME, bCanTimeTrack);

	SetCtrlState(m_eTimeSpent, RTCS_ENABLED);

	// notify parent
	if (bNotify)
		GetParent()->SendMessage(WM_TDCN_TIMETRACK, (WPARAM)GetSafeHwnd(), 0);
}

BOOL CToDoCtrl::DoAddTimeToLogFile()
{
	return DoAddTimeToLogFile(GetSelectedTaskID(), 0.0, TRUE);
}

BOOL CToDoCtrl::DoAddTimeToLogFile(DWORD dwTaskID, double dHours, BOOL bShowDialog)
{
	// sanity check
	if (!HasStyle(TDCS_LOGTIMETRACKING))
		return FALSE;
	
	CString sTaskTitle = GetTaskTitle(dwTaskID), sComment;
	COleDateTime dtWhen = COleDateTime::GetCurrentTime();
	BOOL bAddToTimeSpent = FALSE;
	BOOL bTracked = (dHours != 0.0);
	double dOrgHours = dHours;

	if (bShowDialog)
	{
		CTDLAddLoggedTimeDlg dialog(dwTaskID, sTaskTitle, dHours, this);

		// if we are readonly, we need to prevent
		// the dialog showing 'Add time to time spent'
		BOOL bCanEditTimeSpent = CanEditSelectedTask(TDCA_TIMESPENT);

		if (dialog.DoModal(bCanEditTimeSpent && !bTracked) != IDOK)
			return FALSE;

		// else
		dHours = dialog.GetLoggedHours();
		dtWhen = dialog.GetWhen();
		sComment = dialog.GetComment();

		if (!bTracked)
			bAddToTimeSpent = dialog.GetAddToTimeSpent();
	}

	if (!AddTimeToTaskLogFile(dwTaskID, dHours, dtWhen, sComment, bTracked))
		return FALSE;

	// If the user changed the tracked hours then manually adjust the task's
	// time because it will have already been updated during time-tracking
	if (bTracked && (dHours != dOrgHours))
	{
		return AdjustTaskTimeSpent(dwTaskID, (dHours - dOrgHours));
	}
	else if (bAddToTimeSpent)
	{
		return AdjustTaskTimeSpent(dwTaskID, dHours);
	}

	// else
	return TRUE;
}

LRESULT CToDoCtrl::OnApplyAddLoggedTime(WPARAM wParam, LPARAM lParam)
{
	ASSERT(wParam && lParam);

	if (!wParam || !lParam)
		return 0L;

	const CTDLAddLoggedTimeDlg* pDialog = (const CTDLAddLoggedTimeDlg*)lParam;
	DWORD dwTaskID = wParam;

	if (AddTimeToTaskLogFile(dwTaskID, 
							pDialog->GetLoggedHours(), 
							pDialog->GetWhen(), 
							pDialog->GetComment(), 
							pDialog->IsTracked()))
	{
		if (pDialog->GetAddToTimeSpent())
			return AdjustTaskTimeSpent(dwTaskID, pDialog->GetLoggedHours());
	}

	// else
	return 0L;
}

BOOL CToDoCtrl::AddTimeToTaskLogFile(DWORD dwTaskID, double dHours, const COleDateTime& dtWhen, 
									const CString& sComment, BOOL bTracked)
{
	// sanity check
	if ((dHours == 0.0) && sComment.IsEmpty())
		return FALSE; // sanity check

	CTDCTaskTimeLog log(GetFilePath());

	if (!log.LogTime(dwTaskID, 
						GetTaskTitle(dwTaskID), 
						GetTaskPath(dwTaskID), 
						dHours,
						dtWhen, 
						sComment, 
						bTracked, 
						HasStyle(TDCS_LOGTASKTIMESEPARATELY)))
	{
		UpdateWindow();
		AfxMessageBox(CEnString(IDS_LOGFILELOCKED));

		return FALSE;
	}

	// else
	return TRUE;
}

BOOL CToDoCtrl::AdjustTaskTimeSpent(DWORD dwTaskID, double dHours)
{
	ASSERT(dwTaskID && (dHours != 0.0));

 	TDCTIMEPERIOD time(dHours, TDCU_HOURS);

	if ((GetSelectedCount() == 1) && (GetSelectedTaskID() == dwTaskID))
		return SetSelectedTaskTimeSpent(time, TRUE); // offset

	// else
	CDWordArray aModTaskIDs;
	aModTaskIDs.Add(dwTaskID);

	m_data.SetTaskTimeSpent(dwTaskID, time, TRUE); // offset
	SetModified(TDCA_TIMESPENT, aModTaskIDs);

	return TRUE;
}

CString CToDoCtrl::GetSelectedTaskTimeLogPath() const
{
	DWORD dwTaskID = GetSelectedTaskID();

	if (dwTaskID)
		return CTDCTaskTimeLog(GetFilePath()).GetLogPath(dwTaskID, HasStyle(TDCS_LOGTASKTIMESEPARATELY));

	// else
	return _T("");
}

LRESULT CToDoCtrl::OnGetFont(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	return (LRESULT)::SendMessage(::GetParent(*this), WM_GETFONT, 0, 0);
}

void CToDoCtrl::OnTreeSelChange(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
	*pResult = 0;

	UpdateControls(); 
	GetParent()->PostMessage(WM_TDCN_SELECTIONCHANGE);
	
	// There's a very subtle bug on Windows 8.1 and above:
	// The auto-spell checking of the comments fields sends
	// change notifications even when no edit has been made.
	// This bypasses the usual handling of comments edits
	// and spuriously leaves comments pending for the new
	// selected task(s). If two or more tasks are selected
	// when the next attribute edit is made then the comments
	// of all the selected tasks can end up being cleared!
	// Since I can find no way to detect that an edit is 
	// purely the consequence of spell-checking, I can only
	// work around the problem by migrating the pending flag
	// to effectively clear the changes
	if (m_nCommentsState == CS_PENDING)
	{
		ASSERT(COSVersion() >= OSV_WIN8);
		m_nCommentsState = CS_CHANGED;
	}
}

BOOL CToDoCtrl::SelectTask(DWORD dwTaskID, BOOL bTrue) 
{ 
	if (bTrue)
		dwTaskID = m_data.GetTrueTaskID(dwTaskID);

	HTREEITEM hti = m_taskTree.GetItem(dwTaskID);

	if (hti)
		SelectItem(hti);

	return (hti != NULL);
}

BOOL CToDoCtrl::SelectTasks(const CDWordArray& aTaskIDs, BOOL bTrue)
{
	// sanity check
	ASSERT(aTaskIDs.GetSize());

	if (!aTaskIDs.GetSize())
		return FALSE;

 	return m_taskTree.SelectTasks(aTaskIDs, bTrue);
}

void CToDoCtrl::SelectItem(HTREEITEM hti) 
{ 
	Flush();

	if (m_taskTree.GetSafeHwnd()) 
	{
		if (!m_taskTree.SelectItem(hti))
			UpdateControls(); // disable controls
		
		UpdateSelectedTaskPath();

		// notify parent
		GetParent()->PostMessage(WM_TDCN_SELECTIONCHANGE);
	}
}

void CToDoCtrl::SelectAll() 
{ 
	HandleUnsavedComments();

	if (m_taskTree.SelectAll())
	{
		// load newly selected item
		UpdateControls();
	}
}

BOOL CToDoCtrl::GetColumnAttribAndCtrl(TDC_COLUMN nCol, TDC_ATTRIBUTE& nAttrib, CWnd*& pWnd) const
{
	nAttrib = TDC::MapColumnToAttribute(nCol);
	ASSERT(nAttrib != TDCA_NONE);

	pWnd = GetAttributeCtrl(nAttrib);
	
	return (pWnd != NULL);
}

CWnd* CToDoCtrl::GetAttributeCtrl(TDC_ATTRIBUTE nAttrib) const
{
	UINT nCtrID = TDC::MapAttributeToCtrlID(nAttrib);
	ASSERT(nCtrID != (UINT)-1);
	
	CWnd* pCtrl = GetDlgItem(nCtrID);
	ASSERT_VALID(pCtrl);
	
	return pCtrl;
}

int CToDoCtrl::GetTasks(CTaskFile& tasks, const TDCGETTASKS& filter) const
{
	PrepareTaskfileForTasks(tasks, filter);
	
	// and FINALLY the tasks
	AddTreeChildrenToTaskFile(NULL, tasks, NULL, filter);
	
	return tasks.GetTaskCount();
}

void CToDoCtrl::PrepareTaskfileForTasks(CTaskFile& tasks, const TDCGETTASKS& filter) const
{
	tasks.Reset();
	tasks.SetAvailableAttributes(filter.mapAttribs);

	// ISO date strings
	// must be done first before any tasks are added
	tasks.EnableISODates(HasStyle(TDCS_SHOWDATESINISO));
	
	// custom attributes
	SaveCustomAttributeDefinitions(tasks, filter);
	
	// meta data	
	tasks.SetMetaData(m_mapMetaData);
	
	// file header
	AppendTaskFileHeader(tasks);
}

BOOL CToDoCtrl::XMLHeaderIsUnicode(LPCTSTR szXmlHeader)
{
	return (Misc::ToUpper(szXmlHeader).Find(_T("UTF-16")) != -1);
}

void CToDoCtrl::AppendTaskFileHeader(CTaskFile& tasks) const
{
	ASSERT(XMLHeaderIsUnicode(m_sXmlHeader));

	tasks.SetXmlHeader(m_sXmlHeader);
	tasks.SetXslHeader(m_sXslHeader);
	tasks.SetProjectName(m_sProjectName);
	tasks.SetArchive(m_bArchive);
	tasks.SetEarliestTaskDueDate(m_calculator.GetEarliestDueDate());
	tasks.SetLastModified(m_dtLastTaskMod);
	tasks.SetFilePath(m_sLastSavePath);

	tasks.SetNextUniqueID(m_dwNextUniqueID);
	ASSERT (tasks.GetNextUniqueID() == m_dwNextUniqueID);
	
	if (!HasStyle(TDCS_USES3RDPARTYSOURCECONTROL))
	{
		tasks.SetFileVersion(m_nFileVersion);
	}
}

int CToDoCtrl::GetAllTasks(CTaskFile& tasks) const
{
	return m_exporter.ExportAllTasks(tasks);
}

void CToDoCtrl::HandleUnsavedComments()
{
	if (m_nCommentsState == CS_PENDING)
	{
		UpdateComments(TRUE); // no longer handled by UpdateData
		SetSelectedTaskComments(m_sTextComments, m_customComments, TRUE); // TRUE == internal call

		m_nCommentsState = CS_CHANGED;

		// Update sort if required
		if (m_visColEdit.IsColumnVisible(TDCC_COMMENTSSIZE) && IsSortingBy(TDCC_COMMENTSSIZE))
			Resort();
	}
}

HTREEITEM CToDoCtrl::SetAllTasks(const CTaskFile& tasks)
{
	// Clear existing tree items
	TSH().RemoveAll(FALSE);
	m_taskTree.DeleteAll();

	// Build data structure first 
	m_data.BuildDataModel(tasks);

	// Then tree structure
	return RebuildTree();
}

HTREEITEM CToDoCtrl::RebuildTree(const void* pContext)
{
	// cache current selection and task breadcrumbs before clearing selection
	TDCSELECTIONCACHE cache;
	CacheTreeSelection(cache);
		
	CHoldRedraw hr(*this);
	CWaitCursor cursor;
	CDWordArray aExpanded;
	
	// Allow task tree to prepare for a rebuild
	m_taskTree.OnBeginRebuild();

	if (m_taskTree.GetItemCount())
	{
		m_taskTree.GetExpandedTasks(aExpanded);
		
		TSH().RemoveAll();
		TCH().SelectItem(NULL);
		m_taskTree.DeleteAll();
	}

	HTREEITEM hti = NULL;
	
	if (BuildTreeItem(NULL, m_data.GetStructure(), pContext))
	{
		m_taskTree.SetNextUniqueTaskID(m_dwNextUniqueID);

		hti = m_taskTree.GetChildItem();
	}
	
	// Notify tree that the rebuild is over
	m_taskTree.OnEndRebuild();

	// Then restore previous state
	m_taskTree.SetExpandedTasks(aExpanded);

	if (!RestoreTreeSelection(cache))
	{
		HTREEITEM hti = m_taskTree.GetChildItem(NULL); // select first item
		
		if (hti)
			SelectItem(hti);
		else
			UpdateControls();
	}

	return hti;
}

BOOL CToDoCtrl::BuildTreeItem(HTREEITEM htiParent, const TODOSTRUCTURE* pTDS, const void* pContext)
{
	// sanity checks
#ifdef _DEBUG
	if (pTDS)
	{
		if (htiParent == NULL || htiParent == TVI_ROOT)
		{
			ASSERT (pTDS->GetTaskID() == 0);

			if (pTDS->GetTaskID() != 0)
				return FALSE;
		}
		else
		{
			ASSERT (m_taskTree.GetTaskID(htiParent) == pTDS->GetTaskID());
			
			if (m_taskTree.GetTaskID(htiParent) != pTDS->GetTaskID())
				return FALSE;
		}
	}
	else
	{
		ASSERT(htiParent);
		ASSERT(!m_taskTree.ItemHasChildren(htiParent));

		DWORD dwTaskID = m_taskTree.GetTaskID(htiParent);
		pTDS = m_data.LocateTask(dwTaskID);
		ASSERT(pTDS);
	}
#endif

	if (!pTDS)
		return FALSE;

	// rebuild
	int nSubtask = pTDS->GetSubTaskCount();

	// reverse-order tree-insertion is quicker according to Raymond Chen
	while (nSubtask--)
	{
		const TODOSTRUCTURE* pTDSChild = pTDS->GetSubTask(nSubtask);
		DWORD dwTaskID = pTDSChild->GetTaskID();

		const TODOITEM* pTDIChild = m_data.GetTask(dwTaskID);
		ASSERT(pTDIChild);

		// is this task wanted?
		BOOL bHasChildren = pTDSChild->HasSubTasks();
		BOOL bAddTask = TRUE;

		if (!bHasChildren && pContext)
			bAddTask = WantAddTask(pTDIChild, pTDSChild, pContext);

		if (bAddTask)
		{
			// add this item to tree
			HTREEITEM htiChild = InsertTreeItem(pTDIChild, dwTaskID, htiParent, TVI_FIRST, FALSE);
			ASSERT(htiChild);

			// update next unique ID
			m_dwNextUniqueID = max(m_dwNextUniqueID, dwTaskID + 1);

			// and its children
			if (bHasChildren)
			{
				BuildTreeItem(htiChild, pTDSChild, pContext); // RECURSIVE call

				// if no subtasks got added to the tree then we may need to remove the parent too
				if (!m_taskTree.ItemHasChildren(htiChild))
				{
					BOOL bRemoveTask = (pContext != NULL && !WantAddTask(pTDIChild, pTDSChild, pContext));

					if (bRemoveTask)
					{
						m_taskTree.DeleteItem(htiChild);
					}
				}
			}
		}
	}

	return TRUE;
}

BOOL CToDoCtrl::WantAddTask(const TODOITEM* /*pTDI*/, const TODOSTRUCTURE* /*pTDS*/, const void* /*pContext*/) const
{
	return TRUE;
}

int CToDoCtrl::GetSelectedTasks(CTaskFile& tasks, const TDCGETTASKS& filter) const
{
	if (!GetSelectedCount())
		return 0;
	
	PrepareTaskfileForTasks(tasks, filter);

	BOOL bWantSubtasks = !filter.HasFlag(TDCGSTF_NOTSUBTASKS);
	BOOL bResolveReferences = filter.HasFlag(TDCGSTF_RESOLVEREFERENCES);
	BOOL bWantAllParents = filter.HasFlag(TDCGSTF_ALLPARENTS);
	BOOL bWantImmediateParent = filter.HasFlag(TDCGSTF_IMMEDIATEPARENT);

	// get selected tasks ordered, removing duplicate subtasks 
	// if we will be processing subtasks anyway
	CHTIList selection;
	TSH().CopySelection(selection, bWantSubtasks, TRUE);
	
	// Keep track of the selected tasks added
	CDWordSet aSelTaskIDs;
	POSITION pos = selection.GetHeadPosition();

	while (pos)
	{
		HTREEITEM hti = selection.GetNext(pos);
		DWORD dwTaskID = GetTaskID(hti);
		BOOL bHasParent = m_taskTree.ItemHasParent(hti);

		// do we need to resolve task references?
		if (bResolveReferences)
		{
			DWORD dwRefID = GetTrueTaskID(hti);
			
			if (dwRefID != dwTaskID)
			{
				dwTaskID = dwRefID;
				hti = m_taskTree.GetItem(dwRefID); // true task
			}
		}
		
		// does the user want this task's parent(s) ?
		if ((bWantAllParents || bWantImmediateParent) && bHasParent)
		{
			if (AddTreeItemAndParentToTaskFile(hti, tasks, filter, bWantAllParents, bWantSubtasks))
			{
				aSelTaskIDs.Add(dwTaskID);
			}
		}
		else
		{
			// find the parent task previously added (or not)
			DWORD dwParentID = 0;
			HTASKITEM hParent = NULL;

			if (bHasParent)
			{
				dwParentID = m_taskTree.GetTaskParentID(hti);
				hParent = tasks.FindTask(dwParentID);
			}

			if (AddTreeItemToTaskFile(hti, dwTaskID, tasks, hParent, filter, bWantSubtasks, dwParentID))
			{
				aSelTaskIDs.Add(dwTaskID);
			}
		}
	}

	// extra processing to identify the originally selected tasks
	// in case the user wants to paste as references.
	// Note: References are excluded if bResolveReferences is true
	pos = aSelTaskIDs.GetStartPosition();

	while (pos)
	{
		DWORD dwSelID = aSelTaskIDs.GetNext(pos);
		ASSERT(dwSelID);

		if (!bResolveReferences || !m_data.IsTaskReference(dwSelID))
		{
			HTASKITEM hSelTask = tasks.FindTask(dwSelID);
			ASSERT(hSelTask);

			tasks.SetTaskMetaData(hSelTask, _T("selected"), _T("1"));
		}
	}

	return (tasks.GetTaskCount());
}

BOOL CToDoCtrl::AddTreeItemAndParentToTaskFile(HTREEITEM hti, CTaskFile& tasks, const TDCGETTASKS& filter, 
												BOOL bAllParents, BOOL bWantSubtasks) const
{
	// add parents first, recursively if necessarily
	HTREEITEM htiParent = m_taskTree.GetParentItem(hti);
	HTASKITEM hParent = NULL;
	DWORD dwParentID = 0;

	if (htiParent)
	{
		// Note: we never want parent's subtasks, so we pass FALSE
		if (bAllParents)
		{
			VERIFY(AddTreeItemAndParentToTaskFile(htiParent, tasks, filter, TRUE, FALSE));
		}
		else
		{
			DWORD dwGrandParentID = m_taskTree.GetTaskParentID(htiParent);
			HTASKITEM hGrandParent = tasks.FindTask(dwGrandParentID);

			VERIFY(AddTreeItemToTaskFile(htiParent, 0, tasks, hGrandParent, filter, FALSE, dwGrandParentID));
		}

		// now find the parent we just added
		dwParentID = GetTaskID(htiParent);
		
		hParent = tasks.FindTask(dwParentID);
		ASSERT(hParent);
	}

	// now add item itself
	return AddTreeItemToTaskFile(hti, 0, tasks, hParent, filter, bWantSubtasks, dwParentID);
}

int CToDoCtrl::CacheTreeSelection(TDCSELECTIONCACHE& cache, BOOL bIncBreadcrumbs) const
{
	return m_taskTree.CacheSelection(cache, bIncBreadcrumbs);
}

BOOL CToDoCtrl::RestoreTreeSelection(const TDCSELECTIONCACHE& cache)
{
	if (m_taskTree.RestoreSelection(cache))
	{
		// don't update controls if only one item is selected 
		// and it did not change as a result of the operation
		BOOL bSelChange = !((GetSelectedCount() == 1) && 
							(cache.aSelTaskIDs.GetSize() == 1) &&
							(GetSelectedTaskID() == cache.aSelTaskIDs[0]));
		
		if (bSelChange)
			UpdateControls();

		return TRUE;
	}

	return FALSE;
}

int CToDoCtrl::GetSelectedTaskIDs(CDWordArray& aTaskIDs, BOOL bTrue) const
{
	return m_taskTree.GetSelectedTaskIDs(aTaskIDs, bTrue);
}

int CToDoCtrl::GetSelectedTaskIDs(CDWordArray& aTaskIDs, DWORD& dwFocusedTaskID, BOOL bRemoveChildDupes) const
{
	return m_taskTree.GetSelectedTaskIDs(aTaskIDs, dwFocusedTaskID, bRemoveChildDupes);
}

BOOL CToDoCtrl::PasteTasks(const CTaskFile& tasks, TDC_INSERTWHERE nWhere, BOOL bSelectAll)
{
	if (!CanEditSelectedTask(TDCA_PASTE))
		return FALSE;

	HTREEITEM htiParent = NULL, htiAfter = NULL;

	if (!m_taskTree.GetInsertLocation(nWhere, htiParent, htiAfter))
		return FALSE;

	// merge in any new custom attribute definitions
	CTDCCustomAttribDefinitionArray aAttribDefs;
	
	if (tasks.GetCustomAttributeDefs(aAttribDefs))
	{
		if (m_aCustomAttribDefs.Append(aAttribDefs))
			RebuildCustomAttributeUI();
	}

	// add the tasks
	IMPLEMENT_DATA_UNDO(m_data, TDCUAT_ADD);
	HOLD_REDRAW(*this, m_taskTree);

	// Fix up dependencies if not inserting into new tasklist
	if (GetTaskCount())
	{
		CTaskFile copy(tasks);

		// and always assign new IDs
		PrepareTasksForPaste(copy, TDCR_YES, FALSE);
		return PasteTasksToTree(copy, htiParent, htiAfter, TDCR_NO, bSelectAll);
	}

	// else
	return PasteTasksToTree(tasks, htiParent, htiAfter, TDCR_NO, bSelectAll);
}

BOOL CToDoCtrl::MergeTasks(const CTaskFile& tasks, BOOL bMergeByID)
{
	if (!GetTaskCount())
		return PasteTasks(tasks, TDC_INSERTATTOP, FALSE);

	HTASKITEM hTask = tasks.GetFirstTask();

	if (!hTask)
		return FALSE;

	// cache and clear current selection
	DWORD dwSelID = GetTaskID(TSH().GetFirstItem());

	TCH().SelectItem(NULL);
	TSH().RemoveAll();

	CDWordArray aTaskIDs;

	// add the tasks
	IMPLEMENT_DATA_UNDO(m_data, TDCUAT_ADD);
	HOLD_REDRAW(*this, m_taskTree);
	
	while (hTask)
	{
		VERIFY(MergeTaskWithTree(tasks, hTask, tasks.GetTaskParentID(hTask), bMergeByID, aTaskIDs));

		// next task
		hTask = tasks.GetNextTask(hTask);
	}

	// Merge in any additional custom attributes definitions
	CTDCCustomAttribDefinitionArray aImportedDefs;
				
	if (tasks.GetCustomAttributeDefs(aImportedDefs))
	{
		if (m_aCustomAttribDefs.Append(aImportedDefs))
			RebuildCustomAttributeUI();
	}

	SetModified(TDCA_PASTE, aTaskIDs);
	return TRUE;
}

BOOL CToDoCtrl::MergeTaskWithTree(const CTaskFile& tasks, HTASKITEM hTask, DWORD dwParentTaskID, BOOL bMergeByID, CDWordArray& aNewTaskIDs)
{
	DWORD dwTaskID = 0;

	if (bMergeByID)
	{
		dwTaskID = tasks.GetTaskID(hTask);

		// If this task does not exist then treat it as a new task
		if (!m_data.HasTask(dwTaskID))
			dwTaskID = 0;
	}
	else
	{
		CDWordArray aTaskIDs;

		// Task name must be unique else treat it as a new task
		if (m_matcher.FindTasks(TDCA_TASKNAME, FOP_EQUALS, tasks.GetTaskTitle(hTask), aTaskIDs, FALSE) == 1)
			dwTaskID = aTaskIDs[0];
	}

	if (m_data.HasTask(dwTaskID))
	{
		TODOITEM tdi;
		VERIFY(m_data.GetTaskAttributes(dwTaskID, tdi));

		if (tasks.MergeTaskAttributes(hTask, tdi))
			m_data.SetTaskAttributes(dwTaskID, tdi);
	}
	else 
	{
		dwTaskID = MergeNewTaskIntoTree(tasks, hTask, dwParentTaskID, FALSE); // not subtasks

		if (!dwTaskID)
			return FALSE;

		aNewTaskIDs.Add(dwTaskID);
	}
	ASSERT(dwTaskID);

	// Subtasks
	HTASKITEM hChildTask = tasks.GetFirstTask(hTask);

	while (hChildTask)
	{
		VERIFY(MergeTaskWithTree(tasks, hChildTask, dwTaskID, bMergeByID, aNewTaskIDs));
		hChildTask = tasks.GetNextTask(hChildTask); // next
	}

	return TRUE;
}

DWORD CToDoCtrl::MergeNewTaskIntoTree(const CTaskFile& tasks, HTASKITEM hTask, DWORD dwParentTaskID, BOOL bAndSubtasks)
{
	// Add task to existing parent or root
	HTREEITEM htiParent = NULL;
		
	if (dwParentTaskID)
		htiParent = m_taskTree.GetItem(dwParentTaskID);

	HTREEITEM htiNew = PasteTaskToTree(tasks, hTask, htiParent, TVI_FIRST, TDCR_YES, bAndSubtasks);

	if (!htiNew)
		return 0;

	// else
	return GetTaskID(htiNew);
}

int CToDoCtrl::AddTreeChildrenToTaskFile(HTREEITEM hti, CTaskFile& tasks, HTASKITEM hTask, const TDCGETTASKS& filter) const
{
	HTREEITEM htiChild = m_taskTree.GetChildItem(hti);
	int nChildren = 0;
	
	while (htiChild)
	{
		if (AddTreeItemToTaskFile(htiChild, 0, tasks, hTask, filter, TRUE)) // TRUE = want subtasks
			nChildren++;
		
		// next
		htiChild = m_taskTree.GetNextItem(htiChild);
	}
	
	return nChildren;
}

BOOL CToDoCtrl::AddTreeItemToTaskFile(HTREEITEM hti, DWORD dwTaskID, CTaskFile& tasks, HTASKITEM hParentTask, 
										const TDCGETTASKS& filter, BOOL bWantSubtasks, DWORD dwParentID) const
{
	// Sanity checks
	if (hti == NULL)
	{
		if (bWantSubtasks || (dwTaskID == 0))
		{
			ASSERT(0);
			return FALSE;
		}
	}
	else if (dwTaskID == 0)
	{	
		dwTaskID = GetTaskID(hti);
		ASSERT(dwTaskID);
	}
	else
	{
		ASSERT (GetTaskID(hti) == dwTaskID);
	}
	
	const TODOITEM* pTDI = m_data.GetTask(dwTaskID);
	ASSERT (pTDI);
	
	if (pTDI)
	{
		// Note: task file may already have this task if we've 
		// been auto-adding parents elsewhere
		HTASKITEM hTask = tasks.FindTask(dwTaskID);
		
		if (hTask == NULL)
		{
			hTask = tasks.NewTask(pTDI->sTitle, hParentTask, dwTaskID, dwParentID);
			ASSERT(hTask);
			
			if (!hTask)
				return FALSE;
		}

		// we return TRUE if we match the filter _or_ we have any matching children
		BOOL bMatch = FALSE;
		
		if (bWantSubtasks)
		{
			bMatch = AddTreeChildrenToTaskFile(hti, tasks, hTask, filter);
		}

		if (!bMatch) //  no children matched -> 'Check ourselves'
		{
			if (filter.nFilter == TDCGT_ALL)
			{
				bMatch = TRUE; // always
			}
			else
			{
				BOOL bDone = pTDI->IsDone();
				BOOL bGoodAsDone = (bDone ? TRUE : m_calculator.IsTaskDone(dwTaskID));
			
				switch (filter.nFilter)
				{
				case TDCGT_DUE:
				case TDCGT_DUETOMORROW:
				case TDCGT_DUETHISWEEK:
				case TDCGT_DUENEXTWEEK:
				case TDCGT_DUETHISMONTH:
				case TDCGT_DUENEXTMONTH:
					// remember to check for 'Auto-Due-Today' tasks
					if (!bGoodAsDone)
					{
						if (pTDI->HasDue())
						{
							bMatch = pTDI->IsDue(filter.dateDueBy);
						}
						else if (HasStyle(TDCS_NODUEDATEISDUETODAYORSTART))
						{
							COleDateTime dtDue(CDateHelper::GetDate(DHD_TODAY));

							if (CDateHelper::Max(dtDue, pTDI->dateStart))
								bMatch = (dtDue <= filter.dateDueBy);
						}
					}
					break;

				case TDCGT_DONE:
					bMatch |= (bGoodAsDone || bDone);
					break;

				case TDCGT_NOTDONE:
					bMatch |= !bGoodAsDone; // 'good as' includes 'done'

					// check 'flagged' flag
					if (!bMatch && filter.HasFlag(TDCGTF_KEEPFLAGGED) && pTDI->bFlagged)
						bMatch = TRUE;
					break;

				default:
					bMatch = FALSE;
				}
			}

			// then check 'allocated to' if set
			if (bMatch && !filter.sAllocTo.IsEmpty())
				bMatch = Misc::Contains(filter.sAllocTo, pTDI->aAllocTo, FALSE, TRUE);
		}
		
		if (bMatch)
		{
			const TODOSTRUCTURE* pTDS;
			GET_DATA_TDS(m_data, dwTaskID, pTDS, FALSE);

			m_exporter.ExportTaskAttributes(pTDI, pTDS, tasks, hTask, filter);
		}
		else
		{
			tasks.DeleteTask(hTask);
		}
		
		return bMatch;
	}
	
	return FALSE;
}

void CToDoCtrl::OnGotoFileRef()
{
	if (m_aFileRefs.GetSize())
		GotoFile(m_aFileRefs[0]);
}

void CToDoCtrl::SetFocusToTasks()
{
	if (!m_taskTree.HasFocus())
	{
		// NOTE: if the comments was the last window focused
		// before we were disabled, and we revert the focus
		// to the tree, then the comments gets very confused
		// and will not want to take the focus even though it
		// contains the caret, so we force it to have the focus
		// before switching to the tree.
		SetFocusToComments();
		
		m_taskTree.SetFocus();
	}

	// ensure the selected tree item is visible
	if (!m_taskTree.EnsureSelectionVisible())
		SelectItem(m_taskTree.GetChildItem());
}

void CToDoCtrl::SetFocusToComments()
{
	// ignore if comments are not visible
	if (!::IsWindowVisible(m_ctrlComments))
		return;

	m_ctrlComments.SetFocus();
}

CString CToDoCtrl::GetControlDescription(const CWnd* pCtrl) const
{
	CEnString sText; 

	if (IsChild(pCtrl))
	{
		// comments field
		if (::IsChild(m_ctrlComments, *pCtrl))
		{
			sText.LoadString(IDS_COMMENTSTYPE);
		}
		else if (::IsChild(m_taskTree, *pCtrl))
		{
			sText.LoadString(IDS_TASKTREE);
		}
		else
		{
			sText = GetCtrlLabel(pCtrl);
		}

		sText.Replace(_T("&"), _T(""));
	}

	return sText;
}

CString CToDoCtrl::GetLastSaveFolder() const
{
	return FileMisc::GetFolderFromFilePath(m_sLastSavePath);
}

void CToDoCtrl::MakeRelativePaths(CStringArray& aFilePaths) const
{
	if (aFilePaths.GetSize())
	{
		CString sParentFolder = GetLastSaveFolder();
		
		for (int nFile = 0; nFile < aFilePaths.GetSize(); nFile++)
		{
			CString& sFilePath = aFilePaths[nFile];
			FileMisc::MakeRelativePath(sFilePath, sParentFolder, FileMisc::FolderExists(sFilePath));
		}
	}
}

void CToDoCtrl::MakeFullPaths(CStringArray& aFilePaths) const
{
	CString sParentFolder = GetLastSaveFolder();
	
	for (int nFile = 0; nFile < aFilePaths.GetSize(); nFile++)
	{
		CString& sFilePath = aFilePaths[nFile];

		if (!WebMisc::IsURL(sFilePath))
			FileMisc::MakeFullPath(sFilePath, sParentFolder);
	}
}

LRESULT CToDoCtrl::OnCanDropObject(WPARAM wParam, LPARAM lParam)
{
	if (!wParam || !lParam || IsReadOnly())
	{
		ASSERT(0);
		return FALSE;
	}

	TLDT_DATA* pData = (TLDT_DATA*)wParam;
	CWnd* pTarget = (CWnd*)lParam;

	if (pTarget == &m_taskTree.Tree())
	{
		if (pData->HasFiles())
		{
			if (pData->dwTaskID && !pData->bImportTasks)
				return CanEditSelectedTask(TDCA_FILEREF);

			// Check with parent
			TDCDROPIMPORT data(pData->dwTaskID, *pData->pFilePaths);
			return GetParent()->SendMessage(WM_TDCM_CANIMPORTFROMDROP, (WPARAM)GetSafeHwnd(), (LPARAM)&data);
		}

		if (pData->HasText())
		{
			// Check with parent
			TDCDROPIMPORT data(pData->dwTaskID, pData->GetText());
			return GetParent()->SendMessage(WM_TDCM_CANIMPORTFROMDROP, (WPARAM)GetSafeHwnd(), (LPARAM)&data);
		}

		// else
		ASSERT(0);
		return FALSE;
	}
	else if (pTarget == &m_cbFileRef)
	{
		return CanEditSelectedTask(TDCA_FILEREF);
	}

	// else
	return TRUE;
}

LRESULT CToDoCtrl::OnDropObject(WPARAM wParam, LPARAM lParam)
{
	if (!OnCanDropObject(wParam, lParam))
		return 0L;

	ASSERT(wParam && lParam);
	
	TLDT_DATA* pData = (TLDT_DATA*)wParam;
	CWnd* pTarget = (CWnd*)lParam;

	// Handle dropping files on to a tree item or the 'File Link' field
	CStringArray aFiles;

	if (pData->pOutlookSelection && pData->pOutlookSelection->GetCount())
	{
		int nNumItems = pData->pOutlookSelection->GetCount();

		for (int nItem = 0; nItem < nNumItems; nItem++)
		{
			// format outlook link
			OutlookAPI::_Item obj(pData->pOutlookSelection->Item(COleVariant((short)(nItem + 1))));

			DWORD dwFlags = (Misc::ModKeysArePressed(MKS_SHIFT) ? 0 : OAFMT_NICE);
			aFiles.Add(CMSOutlookHelper::FormatItemAsUrl(obj, dwFlags));
		}
	}
	else if (pData->pFilePaths && pData->pFilePaths->GetSize())
	{
		aFiles.Copy(*(pData->pFilePaths));
	}

	// specific handling
	if (pTarget == &m_taskTree.Tree())
	{
		if (pData->dwTaskID)
			SelectTask(pData->dwTaskID);

		if (aFiles.GetSize())
		{
			if (pData->bImportTasks)
			{
				switch (CreateTasksFromOutlookObjects(pData))
				{
				case -1: // user cancelled
					return 0L;

				case 0:  // failed => not an outlook object
					{
						TDCDROPIMPORT data(pData->dwTaskID, aFiles);
						return GetParent()->SendMessage(WM_TDCM_IMPORTFROMDROP, (WPARAM)GetSafeHwnd(), (LPARAM)&data);
					}

				default:
					break; // all good
				}
			}
			else
			{
				SetSelectedTaskFileRefs(aFiles, TRUE, FALSE);
			}
		}
		else if (pData->HasText())
		{
			TDCDROPIMPORT data(pData->dwTaskID, pData->GetText());
			return GetParent()->SendMessage(WM_TDCM_IMPORTFROMDROP, (WPARAM)GetSafeHwnd(), (LPARAM)&data);
		}
		else
		{
			switch (CreateTasksFromOutlookObjects(pData))
			{
				case -1: // user cancelled
					return 0L;

				case 0:  // failed => not an outlook object
					// TODO
					return 0L;

				default:
					break; // all good
			}
		}

		SetFocusToTasks();
		PostMessage(WM_TDC_FIXUPPOSTDROPSELECTION, 0L, (LPARAM)pData->dwTaskID);
	}
	else if ((pTarget == &m_cbFileRef) && aFiles.GetSize())
	{
		SetSelectedTaskFileRefs(aFiles, TRUE); // append);
		m_cbFileRef.SetFocus();
	}

	// else ignore
	return 0L;
}

int CToDoCtrl::CreateTasksFromOutlookObjects(const TLDT_DATA* pData)
{
	CTaskFile tasks;

	int nRet = CTDCOutlookImportHelper::ImportTasks(pData, IDS_CSV_MUSTMAPTASKTITLE, &tasks);

	switch (nRet)
	{
	case -1: // cancelled
	case  0: // failed ??
		return nRet;
	}

	// add to current tasklist
	HTREEITEM htiInsert = m_taskTree.GetItem(pData->dwTaskID);
	
	if (!htiInsert)
		htiInsert = m_taskTree.HitTestItem(CPoint(1, pData->ptClient.y));
	
	if (htiInsert)
	{
		SelectItem(htiInsert);
		VERIFY(PasteTasks(tasks, TDC_INSERTATBOTTOMOFSELTASK));
	}
	else
	{
		VERIFY(PasteTasks(tasks, TDC_INSERTATBOTTOM));
	}

	return tasks.GetTaskCount();
}

CString CToDoCtrl::GetPreferencesKey(const CString& sSubKey) const
{
	CString sKeyPath;
	
	if (!m_sAltPrefsKey.IsEmpty())
	{
		sKeyPath = CPreferences::KeyFromFile(m_sAltPrefsKey);
	}
	else if (HasFilePath())
	{
		sKeyPath = CPreferences::KeyFromFile(m_sLastSavePath);
	}
	else
	{
		sKeyPath = _T("Default");
	}
	ASSERT(!sKeyPath.IsEmpty());

	CString sKey;
	
	if (!sSubKey.IsEmpty())
		sKey.Format(_T("FileStates\\%s\\%s"), sKeyPath, sSubKey);
	else
		sKey.Format(_T("FileStates\\%s"), sKeyPath);
	
	ASSERT(!sKey.IsEmpty());
	return sKey;
}

void CToDoCtrl::SaveDefaultRecurrence(CPreferences& prefs) const
{
	ASSERT (GetSafeHwnd());
	
	CString sKey = GetPreferencesKey(); // no subkey
	
	prefs.WriteProfileInt(_T("DefaultRecurrence"), _T("RecurFrom"), m_nDefRecurFrom);
	prefs.WriteProfileInt(_T("DefaultRecurrence"), _T("RecurReuse"), m_nDefRecurReuse);
}

void CToDoCtrl::LoadDefaultRecurrence(const CPreferences& prefs)
{
	ASSERT (GetSafeHwnd());
	
	CString sKey = GetPreferencesKey(); // no subkey
	
	m_nDefRecurFrom = prefs.GetProfileEnum(_T("DefaultRecurrence"), _T("RecurFrom"), TDIRO_DUEDATE);
	m_nDefRecurReuse = prefs.GetProfileEnum(_T("DefaultRecurrence"), _T("RecurReuse"), TDIRO_REUSE);
}

void CToDoCtrl::SaveSplitPos(CPreferences& prefs) const
{
	ASSERT (GetSafeHwnd());
	
	CString sKey = GetPreferencesKey(); // no subkey
	prefs.WriteProfileInt(sKey, _T("SplitPos"), m_nCommentsSize);
}

void CToDoCtrl::LoadSplitPos(const CPreferences& prefs)
{
	CString sKey = GetPreferencesKey(); // no subkey
	m_nCommentsSize = prefs.GetProfileInt(sKey, _T("SplitPos"), DEFCOMMENTSIZE);
}

void CToDoCtrl::SaveAttributeVisibility(CTaskFile& tasks) const
{
	if (HasStyle(TDCS_SAVEUIVISINTASKLIST))
		tasks.SetAttributeVisibility(m_visColEdit);
}

void CToDoCtrl::SaveAttributeVisibility(CPreferences& prefs) const
{
	m_visColEdit.Save(prefs, GetPreferencesKey());
}

void CToDoCtrl::LoadAttributeVisibility(const CTaskFile& tasks, const CPreferences& prefs)
{
	// attrib visibility can be stored inside the file or the preferences
	TDCCOLEDITVISIBILITY vis;

	if (tasks.GetAttributeVisibility(vis))
	{
		// update style to match
		m_styles[TDCS_SAVEUIVISINTASKLIST] = TRUE;
	}
	else if (!vis.Load(prefs, GetPreferencesKey()))
	{
		vis = m_visColEdit;
	}

	SetColumnFieldVisibility(vis);
}

void CToDoCtrl::SaveTasksState(CPreferences& prefs, BOOL bRebuildingTree) const
{
	// If rebuilding the tree we only save expanded state 
	m_taskTree.SaveState(prefs, GetPreferencesKey(_T("TaskTree")), bRebuildingTree);
}

HTREEITEM CToDoCtrl::LoadTasksState(const CPreferences& prefs, BOOL bRebuildingTree)
{
	// Only restore expanded state if rebuilding the tree
	HTREEITEM hti = m_taskTree.LoadState(prefs, GetPreferencesKey(_T("TaskTree")), bRebuildingTree);

	if (!bRebuildingTree)
		m_taskTree.RecalcUntrackedColumnWidths();

	return hti;
}

void CToDoCtrl::EndLabelEdit(BOOL bCancel)
{
	m_eTaskName.EndEdit(bCancel);
}

void CToDoCtrl::Flush(BOOL bEndTimeTracking) // called to end current editing actions
{
	CWnd* pFocus = GetFocus();

	if (pFocus)
	{
		if (pFocus == &m_eTaskName)
		{
			EndLabelEdit(FALSE);
		}
		else if (CDialogHelper::IsChildOrSame(&m_cbCategory, pFocus))
		{
			m_cbCategory.Flush();
		}		
		else if (CDialogHelper::IsChildOrSame(&m_cbTags, pFocus))
		{
			m_cbTags.Flush();
		}		
		else if (CDialogHelper::IsChildOrSame(&m_cbStatus, pFocus))
		{
			m_cbStatus.Flush();
		}		
		else if (CDialogHelper::IsChildOrSame(&m_cbAllocBy, pFocus))
		{
			m_cbAllocBy.Flush();
		}		
		else if (CDialogHelper::IsChildOrSame(&m_cbAllocTo, pFocus))
		{
			m_cbAllocTo.Flush();
		}
		else
		{
			CTDCCustomAttributeUIHelper::FlushEditControl(pFocus, this, m_aCustomControls);
		}
	}

	m_treeDragDrop.CancelDrag();

	HandleUnsavedComments();

	if (bEndTimeTracking)
		EndTimeTracking(TRUE, FALSE);
}

TDC_FILE CToDoCtrl::CheckIn()
{
	Flush(TRUE);

	return m_sourceControl.CheckIn();
}

BOOL CToDoCtrl::IsCheckedOut() const 
{ 
	return m_sourceControl.IsCheckedOut();
}

BOOL CToDoCtrl::IsSourceControlled() const 
{ 
	return m_sourceControl.IsSourceControlled();
}

BOOL CToDoCtrl::CanAddToSourceControl(BOOL bAdd) const
{
	return m_sourceControl.CanAddToSourceControl(bAdd);
}

BOOL CToDoCtrl::AddToSourceControl(BOOL bAdd)
{
	return (m_sourceControl.AddToSourceControl(bAdd) == TDCF_SUCCESS);
}

TDC_FILE CToDoCtrl::CheckOut(CString& sCheckedOutTo, BOOL bForce)
{
	CWaitCursor cursor;
	CTaskFile tasks(m_sPassword);

	TDC_FILE nResult = m_sourceControl.CheckOut(tasks, sCheckedOutTo, bForce);

	if (nResult == TDCF_SUCCESS)
	{
		// load tasks
		tasks.Decrypt();
		VERIFY(LoadTasks(tasks));
	}

	return nResult;
}

int CToDoCtrl::FindTasks(const SEARCHPARAMS& params, CResultArray& aResults) const
{
	return TCF().FindTasks(params, aResults, HasDueTodayColor());
}

BOOL CToDoCtrl::HasOverdueTasks() const
{
	return m_calculator.HasOverdueTasks();
}

BOOL CToDoCtrl::HasDueTodayTasks() const
{
	return m_calculator.HasDueTodayTasks();
}

BOOL CToDoCtrl::HasLockedTasks() const
{
	return m_calculator.HasLockedTasks();
}

// External
BOOL CToDoCtrl::SelectTask(const CString& sPart, TDC_SELECTTASK nSelect)
{
	return SelectTask(sPart, nSelect, TDCA_ANYTEXTATTRIBUTE, FALSE, FALSE, FALSE);
}

// Internal
BOOL CToDoCtrl::SelectTask(const CString& sPart, TDC_SELECTTASK nSelect, TDC_ATTRIBUTE nAttrib, 
							BOOL bCaseSensitive, BOOL bWholeWord, BOOL /*bFindReplace*/)
{
	if (!SEARCHPARAM::GetAttribType(nAttrib, FALSE) == FT_STRING)
	{
		ASSERT(0);
		return FALSE;
	}

	SEARCHPARAM rule(nAttrib, FOP_INCLUDES, sPart);
	rule.SetMatchWholeWord(bWholeWord);

	SEARCHPARAMS params;
	params.aRules.Add(rule);
	params.bCaseSensitive = bCaseSensitive;

	HTREEITEM htiStart = NULL; // first item
	BOOL bForwards = TRUE;
	
	switch (nSelect)
	{
	case TDC_SELECTFIRST:
		htiStart = TCH().GetFirstItem();
		break;

	case TDC_SELECTNEXT:
		htiStart = TCH().GetNextItem(GetSelectedItem());
		break;

	case TDC_SELECTNEXTINCLCURRENT:
		htiStart = GetSelectedItem();
		break;

	case TDC_SELECTPREV:
		htiStart = TCH().GetPrevItem(GetSelectedItem());
		bForwards = FALSE;
		break;

	case TDC_SELECTLAST:
		htiStart = TCH().GetLastItem();
		bForwards = FALSE;
		break;

	default:
		ASSERT(0);
		break;
	}

	if (!htiStart)
		return FALSE;

	SEARCHRESULT result;
	HTREEITEM htiMatch = TCF().FindNextTask(htiStart, params, result, bForwards, FALSE);

	if (!htiMatch)
		return FALSE;

	return SelectTask(GetTaskID(htiMatch));
}

void CToDoCtrl::OnLButtonDown(UINT nFlags, CPoint point) 
{
	// is it over the comments splitter
	ASSERT (!m_bSplitting);

	if (IsSplitterVisible() && GetSplitterRect().PtInRect(point))
	{
		m_bSplitting = TRUE;
		SetCapture();
	}
	
	CRuntimeDlg::OnLButtonDown(nFlags, point);
}

void CToDoCtrl::OnLButtonUp(UINT nFlags, CPoint point) 
{
	if (m_bSplitting)
	{
		ReleaseCapture();
		m_bSplitting = FALSE;

        SaveSplitPos(CPreferences());
	}
	
	CRuntimeDlg::OnLButtonUp(nFlags, point);
}

BOOL CToDoCtrl::GetStackCommentsAndControls() const
{
	return ((m_nMaxState == TDCMS_NORMAL) &&
			(m_nCommentsPos == m_nControlsPos) && 
			HasStyle(TDCS_AUTOREPOSCTRLS) &&
			HasStyle(TDCS_ALLOWCOMMENTSSTACKING));
}

int CToDoCtrl::CalcMinCommentSize() const
{
	int nMinCommentsSize = 0; // no minimum

	if (GetStackCommentsAndControls())
	{
		CDlgUnits dlu(this);

		switch (m_nCommentsPos)
		{
		case TDCUIL_RIGHT: // vertical
		case TDCUIL_LEFT:
			// two column widths
			nMinCommentsSize = (dlu.ToPixelsX((2 * CTRLLEN) + CTRLHSPACING) + 1);
			break;
			
		case TDCUIL_BOTTOM: // horizontal
			// two row height
			nMinCommentsSize = (2 * (GetDefaultControlHeight() + dlu.ToPixelsY(LABELHEIGHT))) + dlu.ToPixelsY(CTRLVSPACING);
			break;

		default:
			ASSERT(0);
			break;
		}
	}
	
	return nMinCommentsSize;
}

void CToDoCtrl::ValidateCommentsSize()
{
	CRect rClient;
	GetClientRect(rClient);

	// don't validate for unexpectedly small rects
	if ((rClient.Width() < MINNONCOMMENTWIDTH) || 
		(rClient.Height() < MINNONCOMMENTHEIGHT))
	{
		return;
	}

	int nMaxComments = CalcMaxCommentSize();
	int nMinComments = CalcMinCommentSize();
	int nValidCommentSize = max(nMinComments, min(nMaxComments, m_nCommentsSize));

#ifdef _DEBUG
// 	if (m_nCommentsSize != nValidCommentSize)
// 		TRACE(_T("CToDoCtrl::ValidateCommentsSize(%d -> %d)\n"), m_nCommentsSize, nValidCommentSize);
#endif

	m_nCommentsSize = nValidCommentSize;
}

void CToDoCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_bSplitting)
	{
		int nNewSize = 0, nPrevSize = m_nCommentsSize;
		CRect rSplitter = GetSplitterRect();
		
		switch (m_nCommentsPos)
		{
		case TDCUIL_RIGHT: // vertical
			nNewSize = (m_nCommentsSize - (point.x - rSplitter.left));
			break;
			
		case TDCUIL_LEFT: // vertical
			nNewSize = (m_nCommentsSize + (point.x - rSplitter.right));
			break;
			
		case TDCUIL_BOTTOM: // horizontal
		default:
			nNewSize = (m_nCommentsSize - (point.y - rSplitter.top));
			break;
		}

		// calculate the minimum comments size 
		int nMaxCommentSize = CalcMaxCommentSize();
		int nMinCommentSize = CalcMinCommentSize();

		nNewSize = min(max(nNewSize, nMinCommentSize), nMaxCommentSize);

		if (nNewSize != m_nCommentsSize)
		{
			// calc minimum control rect and only resize if the 
			// controls require less space than we have available
			CRect rCtrls, rClient;
			int nCols, nRows;
			GetClientRect(rClient);

			// NOTE: we need to set the splitter before we 
			// call CalcRequiredControlsRect since it
			// depends on the splitter width to determine its
			// available width/height.
			// BUT if the drag fails we must remember to restore
			// the previous split pos
			m_nCommentsSize = nNewSize;

			CalcRequiredControlsRect(rClient, rCtrls, nCols, nRows, TRUE);
			CRect rIntersect;

			if (rCtrls.IsRectEmpty() || (rIntersect.IntersectRect(rClient, rCtrls) && (rIntersect == rCtrls)))
			{
				Resize(0, 0, TRUE);
				Invalidate(TRUE);
				UpdateWindow();

				return;
			}

			// restore previous drag pos
			m_nCommentsSize = nPrevSize;
		}
	}
	
	CRuntimeDlg::OnMouseMove(nFlags, point);
}

CRect CToDoCtrl::GetSplitterRect() const
{
	BOOL bStackCommentsAndControls = GetStackCommentsAndControls();

	// calc splitter rect from comment pos/size
	CRect rSplitter;
	GetClientRect(rSplitter);

	CRect rTasks;
	m_taskTree.GetWindowRect(rTasks);
	ScreenToClient(rTasks);

	switch (m_nCommentsPos)
	{
	case TDCUIL_RIGHT: // vertical
		{
			rSplitter.right -= m_nCommentsSize;
			rSplitter.left = (rSplitter.right - SPLITSIZE);

			ASSERT(rSplitter.Width() == SPLITSIZE);
		}
		break;
		
	case TDCUIL_LEFT:
		{
			rSplitter.left = m_nCommentsSize;
			rSplitter.right = (rSplitter.left + SPLITSIZE);

			ASSERT(rSplitter.Width() == SPLITSIZE);
		}
		break;
		
	case TDCUIL_BOTTOM: // horizontal
		{
			rSplitter.bottom -= (m_nCommentsSize + 2);
			rSplitter.top = (rSplitter.bottom - SPLITSIZE);

			ASSERT(rSplitter.Height() == SPLITSIZE);
		}
		break;
	}
	
	return rSplitter;
}

BOOL CToDoCtrl::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message) 
{
	if (IsSplitterVisible())
	{
		CPoint ptClient(::GetMessagePos());
		ScreenToClient(&ptClient);
		
		if (GetSplitterRect().PtInRect(ptClient))
		{
			switch (m_nCommentsPos)
			{
			case TDCUIL_RIGHT: // vertical
			case TDCUIL_LEFT:
				::SetCursor(AfxGetApp()->LoadCursor(AFX_IDC_HSPLITBAR));
				break;
				
			case TDCUIL_BOTTOM: // horizontal
				::SetCursor(AfxGetApp()->LoadCursor(AFX_IDC_VSPLITBAR));
				break;
			}
			
			return TRUE;
		}
	}
	
	// else
	return CRuntimeDlg::OnSetCursor(pWnd, nHitTest, message);
}

void CToDoCtrl::OnCaptureChanged(CWnd *pWnd) 
{
	m_bSplitting = FALSE;
	
	CRuntimeDlg::OnCaptureChanged(pWnd);
}

void CToDoCtrl::OnShowWindow(BOOL bShow, UINT nStatus) 
{
	CRuntimeDlg::OnShowWindow(bShow, nStatus);
	
	if (!bShow)
		m_findReplace.DestroyDialog();
}

LRESULT CToDoCtrl::OnTimeUnitsChange(WPARAM wParam, LPARAM /*lParam*/)
{
	int nRecalcTime = IDNO;
	BOOL bWantQueryRecalc = (GetSelectedCount() > 1);
	
	if (!bWantQueryRecalc) // one item selected
	{
		TDCTIMEPERIOD time;

		// see if the time is non-zero and if so we prompt
		switch (wParam)
		{
		case IDC_TIMEEST:
			bWantQueryRecalc = (GetSelectedTaskTimeEstimate(time) && (time.dAmount > 0.0));
			break;
		
		case IDC_TIMESPENT:
			bWantQueryRecalc = (GetSelectedTaskTimeSpent(time) && (time.dAmount > 0.0));
			break;

		default:
			ASSERT(CTDCCustomAttributeUIHelper::IsCustomEditControl(wParam));
			break;
		}
	}

	if (bWantQueryRecalc)
		nRecalcTime = CMessageBox::AfxShow(IDS_TDC_RECALCTITLE, IDS_TDC_RECALCPROMPT, MB_ICONQUESTION | MB_YESNOCANCEL);

	if (nRecalcTime != IDCANCEL)
	{
		DWORD dwFlags = (UTF_TIMEUNITSONLY | (nRecalcTime == IDYES ? UTF_RECALCTIME : 0));

		switch (wParam)
		{
		case IDC_TIMEEST:
			UpdateTask(TDCA_TIMEEST, dwFlags); 
			break;
		
		case IDC_TIMESPENT:
			UpdateTask(TDCA_TIMESPENT, dwFlags); 
			break;

		default:
			OnCustomAttributeChange(wParam);
			break;
		}
	}
	
	return (nRecalcTime == IDCANCEL);
}

void CToDoCtrl::SpellcheckSelectedTask(BOOL bTitle) 
{
	Flush();

	IMPLEMENT_DATA_UNDO(m_data, TDCUAT_EDIT);

	// one off spell check
	CSpellCheckDlg dialog;
	dialog.SetDictionaryDownloadUrl(DICTIONARY_URL);
	
	SpellcheckItem(GetSelectedItem(), &dialog, bTitle, TRUE);
}

LRESULT CToDoCtrl::OnCommentsWantSpellCheck(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	SpellcheckSelectedTask(FALSE); 

	return 0L;
}

BOOL CToDoCtrl::CanSpellcheckSelectedTaskComments()
{
	if (!CanSpellcheckComments())
		return FALSE;

	return (CanEditSelectedTask(TDCA_COMMENTS) && !GetSelectedTaskComments().IsEmpty());
}

BOOL CToDoCtrl::CanSpellcheckComments()
{
	return (NULL != m_ctrlComments.GetSpellCheckInterface());
}

void CToDoCtrl::Spellcheck()
{
	IMPLEMENT_DATA_UNDO_EDIT(m_data);

	// top level items
	CSpellCheckDlg dialog;
	dialog.SetDictionaryDownloadUrl(DICTIONARY_URL);

	HTREEITEM hti = m_taskTree.GetChildItem();
	
	while (hti)
	{
		if (!SpellcheckItem(hti, &dialog))
			return;
		
		hti = m_taskTree.GetNextItem(hti);
	}
}

BOOL CToDoCtrl::SpellcheckItem(HTREEITEM hti, CSpellCheckDlg* pSpellChecker, BOOL bCheckTitle, BOOL bNotifyNoErrors)
{
	ASSERT(pSpellChecker);
	
	if (!pSpellChecker)
		return FALSE;

	if (!bCheckTitle && !CanSpellcheckComments())
		return TRUE;
	
	DWORD dwTaskID = GetTaskID(hti);
	const TODOITEM* pTDI = GetTask(dwTaskID);
	ASSERT (pTDI);
	
	if (pTDI)
	{
		if (bCheckTitle)
		{
			CString sTitle = m_data.GetTaskTitle(dwTaskID);
			
			if (sTitle.IsEmpty())
				return TRUE;

			// else
			SelectItem(hti);
			pSpellChecker->SetText(sTitle);
		}
		else
		{
			SelectItem(hti);
			pSpellChecker->SetSpellCheck(m_ctrlComments.GetSpellCheckInterface());
		}
		
		int nRet = pSpellChecker->DoModal(CPreferences(), TRUE);
		UpdateWindow();
			
		if ((nRet == IDOK) && CanEditSelectedTask(TDCA_TASKNAMEORCOMMENTS))
		{
			int nChange = SET_NOCHANGE;
			
			if (bCheckTitle)
			{
				CString sTitle = pSpellChecker->GetCorrectedText();
				nChange = m_data.SetTaskTitle(dwTaskID, sTitle);
				
				if (nChange == SET_CHANGE)
				{
					InvalidateItem(hti, TRUE);
				}
			}
			else if (pSpellChecker->MadeChanges()) 
			{
				UpdateTask(TDCA_COMMENTS);
				nChange = SET_CHANGE;
			}
			
			if (nChange == SET_CHANGE)
			{
				CDWordArray aModTaskIDs;
				aModTaskIDs.Add(dwTaskID);

				SetModified(TDCA_TASKNAME, aModTaskIDs);
			}
		}
		else if (nRet == IDNOERRORS && bNotifyNoErrors)
		{
			if (bCheckTitle)
				CMessageBox::AfxShow(IDS_TDC_SPELLCHECK_TITLE, IDS_TDC_NOTITLESPELLERRORS);
			else
				CMessageBox::AfxShow(IDS_TDC_SPELLCHECK_TITLE, IDS_TDC_NOCOMMENTSPELLERRORS);
		}
		else if (nRet == IDCANCEL)
		{
			return FALSE;
		}

		return TRUE;
	}
	
	// else
	ASSERT(0);
	return FALSE;
}

BOOL CToDoCtrl::SpellcheckItem(HTREEITEM hti, CSpellCheckDlg* pSpellChecker)
{
	if (!SpellcheckItem(hti, pSpellChecker, TRUE, FALSE) || 
		!SpellcheckItem(hti, pSpellChecker, FALSE, FALSE))
		return FALSE;
	
	// subtasks
	HTREEITEM htiSub = m_taskTree.GetChildItem(hti);
	
	while (htiSub)
	{
		if (!SpellcheckItem(htiSub, pSpellChecker))
			return FALSE;
		
		htiSub = m_taskTree.GetNextItem(htiSub, TVGN_NEXT);
	}
	
	return TRUE;
}

BOOL CToDoCtrl::DoFindReplace(TDC_ATTRIBUTE nAttrib)
{
	return m_findReplace.DoFindReplace(nAttrib);
}

BOOL CToDoCtrl::CanDoFindReplace(TDC_ATTRIBUTE nAttrib) const
{
	return m_findReplace.CanDoFindReplace(nAttrib);
}

LRESULT CToDoCtrl::OnFindReplaceSelectNextTask(WPARAM wParam, LPARAM /*lParam*/)
{
	return SelectTask(m_findReplace.GetSearchFor(), 
						(TDC_SELECTTASK)wParam,
						m_findReplace.GetAttribute(),
						m_findReplace.WantCaseSensitive(), 
						m_findReplace.WantWholeWord(), TRUE);
}

LRESULT CToDoCtrl::OnFindReplaceSelectedTask(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	return FindReplaceSelectedTaskAttribute();
}

LRESULT CToDoCtrl::OnFindReplaceAllTasks(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	// Treat as a single edit
	IMPLEMENT_DATA_UNDO_EDIT(m_data);

	// Start at the beginning
	if (SelectTask(m_findReplace.GetSearchFor(), 
					TDC_SELECTFIRST, 
					m_findReplace.GetAttribute(),
					m_findReplace.WantCaseSensitive(), 
					m_findReplace.WantWholeWord(), 
					TRUE))
	{
		do 
		{
			ASSERT(GetSelectedCount() == 1);

			if (!FindReplaceSelectedTaskAttribute())
				break;
		} 
		while (SelectTask(m_findReplace.GetSearchFor(), 
							TDC_SELECTNEXT, 
							m_findReplace.GetAttribute(),
							m_findReplace.WantCaseSensitive(), 
							m_findReplace.WantWholeWord(), 
							TRUE));

		return TRUE;
	}

	MessageBeep(MB_ICONHAND);
	return FALSE;
}

LRESULT CToDoCtrl::OnFindReplaceGetExclusionRect(WPARAM wParam, LPARAM lParam)
{
	ASSERT(lParam);

	BOOL bFirstTime = wParam, bUpdown = bFirstTime;
	CRect rExclude;

	switch (m_findReplace.GetAttribute())
	{
	case TDCA_TASKNAME:
		if (bFirstTime)
			m_taskTree.Tree().GetWindowRect(rExclude);
		else
			GetLabelEditRect(rExclude);
		break;

	case TDCA_COMMENTS:
		m_ctrlComments.GetWindowRect(rExclude);

		bUpdown = (m_nCommentsPos == TDCUIL_BOTTOM);
		break;

	default:
		ASSERT(0);
		break;
	}
	
	*((LPRECT)lParam) = rExclude;
	return bUpdown;
}

LRESULT CToDoCtrl::OnFindReplaceMsg(WPARAM wParam, LPARAM lParam)
{
	m_findReplace.HandleCmd(wParam, lParam);
	return 0;
}

BOOL CToDoCtrl::FindReplaceSelectedTaskAttribute()
{
	TDC_ATTRIBUTE nAttrib = m_findReplace.GetAttribute();

	if (!m_findReplace.IsReplacing() ||	!CanEditSelectedTask(nAttrib))
	{
		ASSERT(0);
		return FALSE;
	}

	CString sSelAttrib;

	switch (nAttrib)
	{
	case TDCA_TASKNAME:
		sSelAttrib = GetSelectedTaskTitle();
		break;

	case TDCA_COMMENTS:
		sSelAttrib = GetSelectedTaskComments();
		break;
	}

	if (Misc::Replace(m_findReplace.GetSearchFor(), 
						m_findReplace.GetReplaceWith(), 
						sSelAttrib, 
						m_findReplace.WantCaseSensitive(), 
						m_findReplace.WantWholeWord()))
	{
		Misc::Trim(sSelAttrib);

		switch (nAttrib)
		{
		case TDCA_TASKNAME:
			if (SetSelectedTaskTitle(sSelAttrib, FALSE))
				return TRUE;
			break;

		case TDCA_COMMENTS:
			if (m_ctrlComments.FindReplaceAll(m_findReplace.GetSearchFor(), 
											m_findReplace.GetReplaceWith(), 
											m_findReplace.WantCaseSensitive(), 
											m_findReplace.WantWholeWord()))
			{
				UpdateComments(TRUE);

				if (SetSelectedTaskComments(m_sTextComments, m_customComments, TRUE))
					return TRUE;
			}
			break;
		}
	}

	MessageBeep(MB_ICONHAND);
	return FALSE;
}

int CToDoCtrl::GetSelectedTaskCustomAttributeData(CTDCCustomAttributeDataMap& mapData, BOOL bFormatted) const
{
	mapData.RemoveAll();
	
	int nDef = m_aCustomAttribDefs.GetSize();
	TDCCADATA data;
	
	while (nDef--)
	{
		const TDCCUSTOMATTRIBUTEDEFINITION& def = m_aCustomAttribDefs.GetData()[nDef];
		GetSelectedTaskCustomAttributeData(def.sUniqueID, data, bFormatted);

		mapData[def.sUniqueID] = data;
	}
	
	return mapData.GetCount();
}

void CToDoCtrl::SetDefaultTaskAttributeValues(const TODOITEM& tdi)
{
	m_tdiDefault = tdi;
	m_cfDefault = m_tdiDefault.cfComments;

	m_data.SetDefaultCommentsFormat(m_cfDefault);
	m_data.SetDefaultTimeUnits(tdi.timeEstimate.nUnits, tdi.timeSpent.nUnits);
	
	TODOITEM::SetModifierName(tdi.sCreatedBy); // 'this' user

	// set default task creation date to zero so that new tasks
	// always get the current date
	CDateHelper::ClearDate(m_tdiDefault.dateCreated);

	UpdateDefaultTaskCustomAttributeValues();
}

void CToDoCtrl::UpdateDefaultTaskCustomAttributeValues()
{
	CTDCCustomAttributeDataMap dataDef;
	int nCust = m_aCustomAttribDefs.GetSize();

	while (nCust--)
	{
		const TDCCUSTOMATTRIBUTEDEFINITION& attribDef = m_aCustomAttribDefs[nCust];

		// The only default custom attribute value we currently support
		// is single selection lists with default data but without a 'blank' item
		// ie. A blank default item is not appropriate
		if (attribDef.bEnabled && 
			!attribDef.IsMultiList() && 
			attribDef.aDefaultListData.GetSize() && 
			attribDef.HasFeature(TDCCAF_EXCLUDEBLANKITEM))
		{
			dataDef.SetAt(attribDef.sUniqueID, attribDef.aDefaultListData[0]);
		}
	}

	m_tdiDefault.SetCustomAttributeValues(dataDef);
}

LRESULT CToDoCtrl::OnEEBtnClick(WPARAM wParam, LPARAM lParam)
{
	switch (wParam)
	{
	case IDC_TIMESPENT:
		if (GetSelectedCount() == 1)
		{
			HandleUnsavedComments();

			switch (lParam)
			{
			case ID_TIME_TRACK:
				ToggleTimeTracking(GetSelectedItem());
				break;

			case ID_ADD_TIME:
				DoAddTimeToLogFile();
				break;
			}
		}
		break;

	case IDC_DEPENDS:
		GotoSelectedTaskDependency();
		break;
	}
	
	return 0L;
}

BOOL CToDoCtrl::GotoSelectedReferenceTaskTarget()
{
	// traverse the selected items adding any
	// reference task's target to a list
	CDWordArray aTaskIDs;

	POSITION pos = TSH().GetFirstItemPos();
			
	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);

		if (m_data.IsTaskReference(dwTaskID))
			aTaskIDs.Add(dwTaskID);
	}

	// select any found items
	if (aTaskIDs.GetSize())
		return SelectTasks(aTaskIDs, TRUE); 

	// else
	return FALSE;
}

BOOL CToDoCtrl::GotoSelectedTaskReferences()
{
	// traverse the selected items adding any
	// reference task's target to a list
	CDWordArray aTaskRefIDs;
	POSITION pos = TSH().GetFirstItemPos();
			
	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);

		if (!m_data.IsTaskReference(dwTaskID))
		{
			CDWordArray aRefIDs;

			if (m_data.GetReferencesToTask(dwTaskID, aRefIDs))
				aTaskRefIDs.Append(aRefIDs);
		}
	}

	// select any found items
	if (aTaskRefIDs.GetSize())
		return SelectTasks(aTaskRefIDs, FALSE);

	// else
	return FALSE;
}

BOOL CToDoCtrl::GotoSelectedTaskLocalDependencies()
{
	// traverse the selected items adding any dependencies to a list
	CDWordArray aDependIDs;
	POSITION pos = TSH().GetFirstItemPos();
			
	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);
		CDWordArray aLocalDepends;

		if (m_data.GetTaskLocalDependencies(dwTaskID, aLocalDepends))
			aDependIDs.Append(aLocalDepends);
	}

	// select any found items
	if (aDependIDs.GetSize())
		return SelectTasks(aDependIDs, FALSE);

	// else
	return FALSE;
}

BOOL CToDoCtrl::GotoSelectedTaskLocalDependents()
{
	// traverse the selected items adding any dependents to a list
	CDWordArray aDependentIDs;
	POSITION pos = TSH().GetFirstItemPos();
			
	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);
		CDWordArray aLocalDependents;

		if (m_data.GetTaskLocalDependents(dwTaskID, aLocalDependents))
			aDependentIDs.Append(aLocalDependents);
	}

	// select any found items
	if (aDependentIDs.GetSize())
		return SelectTasks(aDependentIDs, FALSE);

	// else
	return FALSE;
}

BOOL CToDoCtrl::GotoSelectedTaskDependency()
{
	CStringArray aDepends;
	GetSelectedTaskDependencies(aDepends);
	
	if (aDepends.GetSize())
		return ShowTaskLink(aDepends[0], FALSE);

	// else
	return FALSE;
}

BOOL CToDoCtrl::EditSelectedTaskRecurrence()
{
	if (CanEditSelectedTask(TDCA_RECURRENCE) && !IsSelectedTaskDone())
		return m_eRecurrence.DoEdit(TRUE); // TRUE forces an edit

	// else
	return FALSE;
}

void CToDoCtrl::OnTimer(UINT nIDEvent) 
{
	switch (nIDEvent)
	{
	case TIMER_TRACK:
		IncrementTrackedTime(FALSE);
		break;

	case TIMER_MIDNIGHT:
		{
			// check if we've just passed midnight, in which case some tasks
			// may have just become due
			static time_t tPrev = time(NULL); // once only
			time_t tNow = time(NULL);

			if ((tNow / DAYINSECS) > (tPrev / DAYINSECS))
			{
				OnTimerMidnight();
			}

			tPrev = tNow;
		}
		break;
	}

	
	CRuntimeDlg::OnTimer(nIDEvent);
}

void CToDoCtrl::OnTimerMidnight()
{
	Invalidate();
}

void CToDoCtrl::IncrementTrackedTime(BOOL bEnding)
{
	// if we are editing the title of the task being tracked then 
	// leave immediately and wait until the editing has ended
	if (!bEnding && IsTaskLabelEditing() && m_timeTracking.IsTracking(GetSelectedTaskID()))
		return;

	double dIncrement = m_timeTracking.IncrementTrackedTime(); // hours
	
	if (dIncrement > 0.0)
	{
		DWORD dwTaskID = m_timeTracking.GetTrackedTaskID(TRUE);
		ASSERT(dwTaskID);

		// Tracked/logged time is always in hours
		m_dTrackedTimeElapsedHours += dIncrement;

		TDCTIMEPERIOD time(dIncrement, TDCU_HOURS);
		
		if ((dwTaskID == GetSelectedTaskID()) && (GetSelectedCount() == 1))
		{
			// this will also update the Time Spent field
			SetSelectedTaskTimeSpent(time, TRUE); // offset
		}
		else
		{
			CDWordArray aModTaskIDs;
			aModTaskIDs.Add(dwTaskID);

			m_data.SetTaskTimeSpent(dwTaskID, time, TRUE); // offset
			SetModified(TDCA_TIMESPENT, aModTaskIDs);
		}

		// Is a reminder due?
		if (m_timeTracking.IsReminderDue())
		{
			TRACE(_T("Sending time track reminder notification as %.2f seconds\n"), 
						(m_dTrackedTimeElapsedHours / TICKS2HOURS) / 1000);

			m_timeTracking.ResetReminderIsDue();
			GetParent()->SendMessage(WM_TDCN_TIMETRACKREMINDER, (WPARAM)GetSafeHwnd(), dwTaskID);
		}
	}
}

LRESULT CToDoCtrl::OnCustomUrl(WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(wParam);
	ASSERT(wParam == (WPARAM)m_ctrlComments.GetDlgCtrlID());
	
	// FALSE says only handle "tdl://" links
	GotoFile((LPCTSTR)lParam, FALSE);
	
	return 0;
}

void CToDoCtrl::SelectTasksInHistory(BOOL bForward) 
{ 
	HandleUnsavedComments();

	if (m_taskTree.SelectTasksInHistory(bForward))
		UpdateControls();
}

LRESULT CToDoCtrl::OnFileEditWantIcon(WPARAM wParam, LPARAM lParam)
{
	if (wParam == IDC_FILEPATH)
	{
		const CString& sUrl = (LPCTSTR)lParam;
		
		if (TODOITEM::IsTaskLink(sUrl, TRUE))
			return (LRESULT)GraphicsMisc::GetAppWindowIcon(FALSE);
	}
	
	return 0;
}

LRESULT CToDoCtrl::OnFileEditDisplayFile(WPARAM wParam, LPARAM lParam)
{
	if (wParam == IDC_FILEPATH)
		return GotoFile((LPCTSTR)lParam);
	
	return 0;
}

BOOL CToDoCtrl::GotoFile(const CString& sFile, BOOL bShellExecute)
{
	if (sFile.IsEmpty())
		return FALSE;
	
	if (TODOITEM::IsTaskLink(sFile, TRUE))
	{
		ShowTaskLink(sFile, TRUE);
		return TRUE;
	}
	else if (bShellExecute)
	{
		// set the current working directory to that of the tasklist
		CString sTasklistFolder = GetLastSaveFolder();

		// try to run it but disable default error handling
		if (CFileEdit::GotoFile(*this, sFile, sTasklistFolder, FALSE) >= SE_ERR_SUCCESS)
			return TRUE;
	}

	// all else, pass to parent
	return GetParent()->SendMessage(WM_TDCM_FAILEDLINK, (WPARAM)GetSafeHwnd(), (LPARAM)(LPCTSTR)sFile);
}

void CToDoCtrl::ExpandTasks(TDC_EXPANDCOLLAPSE nWhat, BOOL bExpand)
{
	if (!CanExpandTasks(nWhat, bExpand))
		return;

	switch (nWhat)
	{
	case TDCEC_ALL:
		m_taskTree.ExpandAll(bExpand);
		break;

	case TDCEC_SELECTED:
		{
			POSITION pos = TSH().GetFirstItemPos();
			
			while (pos)
			{
				HTREEITEM hti = TSH().GetNextItem(pos);
				m_taskTree.ExpandItem(hti, bExpand, TRUE);
			}
		}
		break;

	case TDCEC_DUE:
		{
			// build due date search
			SEARCHPARAMS params;
			params.aRules.Add(SEARCHPARAM(TDCA_DUEDATE, FOP_ON_OR_BEFORE, CDateHelper::GetDate(DHD_TODAY)));
			params.bIgnoreDone = true;

			SearchAndExpand(params, bExpand);
		}
		break;

	case TDCEC_STARTED:
		{
			// build due date search
			SEARCHPARAMS params;
			params.aRules.Add(SEARCHPARAM(TDCA_STARTDATE, FOP_ON_OR_BEFORE, CDateHelper::GetDate(DHD_TODAY)));
			params.bIgnoreDone = true;

			SearchAndExpand(params, bExpand);
		}
		break;
	}

	// if collapsing, move the selection to a visible parent item
	if (!bExpand)
	{
		HTREEITEM htiSel = TSH().GetFirstItem(), hti = htiSel;

		while (hti && !TCH().IsParentItemExpanded(hti))
			hti = m_taskTree.GetParentItem(hti);

		if (hti != htiSel)
			SelectItem(hti);
		else
			UpdateSelectedTaskPath();
	}
}

void CToDoCtrl::SearchAndExpand(const SEARCHPARAMS& params, BOOL bExpand)
{
	// perform the search
	CResultArray aResults;
	int nNumRes = FindTasks(params, aResults);
	
	// do the expansion
	for (int nRes = 0; nRes < nNumRes; nRes++)
	{
		HTREEITEM hti = m_taskTree.GetItem(aResults[nRes].dwTaskID);
		
		if (hti)
			m_taskTree.ExpandItem(hti, bExpand, TRUE);
	}
}

BOOL CToDoCtrl::CanExpandTasks(TDC_EXPANDCOLLAPSE nWhat, BOOL bExpand) const
{
	if (m_taskTree.GetItemCount() == 0)
		return FALSE;
		
	switch (nWhat)
	{
	case TDCEC_ALL:
		if (bExpand)
			return m_taskTree.TCH().IsAnyItemCollapsed();
		else
			return m_taskTree.TCH().IsAnyItemExpanded();
		break;

	case TDCEC_SELECTED:
		{
			int nFullyExpanded = TSH().IsSelectionExpanded(TRUE);
			
			if (nFullyExpanded == -1)	// selected items have no children
			{
				return FALSE; // can neither expand nor collapse
			}
			else if (bExpand)
			{
				return !nFullyExpanded;
			}
			
			// else
			return TSH().IsSelectionExpanded(FALSE);
		}
		break;

	case TDCEC_DUE:
		break;

	case TDCEC_STARTED:
		break;
	}

	// all else
	return TRUE;
}

CString CToDoCtrl::GetClipboardID() const
{
	CString sClipID = CPreferences::KeyFromFile(GetFilePath(), FALSE);

	if (sClipID.IsEmpty())
		sClipID = _T("New_Tasklist");

	return sClipID;
}

BOOL CToDoCtrl::GetClipboardID(CString& sClipID, BOOL bArchive) const
{
	CString sFilePath;

	if (bArchive && !GetArchivePath(sFilePath))
	{
		return FALSE;
	}
	else if (sFilePath.IsEmpty())
	{
		sFilePath = GetFilePath();
	}

	if (sFilePath.IsEmpty())
		sClipID = _T("New_Tasklist");
	else
		sClipID = CPreferences::KeyFromFile(sFilePath, FALSE);

	return TRUE;
}

LRESULT CToDoCtrl::OnTDCHasClipboard(WPARAM /*wParam*/, LPARAM lParam)
{
	BOOL bCheckID = (lParam != FALSE);
	return !IsClipboardEmpty(bCheckID);
}

LRESULT CToDoCtrl::OnTDCGetClipboard(WPARAM wParam, LPARAM lParam)
{
	if (OnTDCHasClipboard(wParam, lParam))
	{
		static CTaskFile tasks;

		if (CTaskClipboard::GetTasks(tasks, GetClipboardID()))
 			return (LRESULT)&tasks;
	}

	// else
	return NULL;
}

LRESULT CToDoCtrl::OnTDCDoTaskLink(WPARAM /*wParam*/, LPARAM lParam)
{
	return ShowTaskLink((LPCTSTR)lParam, TRUE);
}

LRESULT CToDoCtrl::OnTDCFailedLink(WPARAM /*wParam*/, LPARAM lParam)
{
	// Handle relative file path links
	LPCTSTR szLink = (LPCTSTR)lParam;

	if (!CMSOutlookHelper::IsOutlookUrl(szLink) && ::PathIsRelative(szLink))
	{
		CString sLink = FileMisc::GetFullPath(szLink, m_taskTree.GetTasklistFolder());
		
		if (FileMisc::Run(*this, sLink) >= SE_ERR_SUCCESS)
			return 0L;
	}

	// else forward on to our parent
	return GetParent()->SendMessage(WM_TDCM_FAILEDLINK, (WPARAM)GetSafeHwnd(), lParam);
}

LRESULT CToDoCtrl::OnTDCTaskIsDone(WPARAM wParam, LPARAM lParam)
{
	// forward on to our parent
	return GetParent()->SendMessage(WM_TDCM_ISTASKDONE, wParam, lParam);
}

BOOL CToDoCtrl::ShowTaskLink(const CString& sLink, BOOL bURL)
{
	if (sLink.IsEmpty())
		return FALSE;

	CString sFile;
	DWORD dwTaskID = 0;

	if (!ParseTaskLink(sLink, bURL, dwTaskID, sFile))
		return FALSE;

	// if there's a file attached then pass to parent because we can't handle it.
	if (!sFile.IsEmpty())
	{
		return GetParent()->SendMessage(WM_TDCM_SELECTTASK, dwTaskID, (LPARAM)(LPCTSTR)sFile);
	}
	else if (dwTaskID)
	{
		if (SelectTask(dwTaskID))
		{
			SetFocusToTasks();
			return TRUE;
		}
		else
		{
			CMessageBox::AfxShow(IDS_TDC_TASKIDNOTFOUND_TITLE, IDS_TDC_TASKIDNOTFOUND);
		}
	}
	else
	{
		CMessageBox::AfxShow(IDS_TDC_ZEROINVALIDTASKID_TITLE, IDS_TDC_ZEROINVALIDTASKID);
	}

	return FALSE;
}

void CToDoCtrl::OnSelChangeCommentsType()
{
	HandleUnsavedComments();

	BOOL bMixedSelection = (m_cfComments.IsEmpty());
	m_ctrlComments.GetSelectedFormat(m_cfComments);

	IMPLEMENT_DATA_UNDO_EDIT(m_data);

	// modify the comments type of the selected tasks
	// without clearing any custom comments
	BOOL bChange = FALSE;
	POSITION pos = TSH().GetFirstItemPos();
	CDWordArray aModTaskIDs;
		
	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);

		if (!HandleModResult(dwTaskID, m_data.SetTaskCommentsType(dwTaskID, m_cfComments), aModTaskIDs))
			return;
	}

	if (aModTaskIDs.GetSize())
		SetModified(TDCA_COMMENTS, aModTaskIDs);

	// update comments control state if previously in a mixed state
	if (bMixedSelection)
		UpdateControls();
}

void CToDoCtrl::OnSettingChange(UINT uFlags, LPCTSTR lpszSection)
{
    CRuntimeDlg::OnSettingChange(uFlags, lpszSection);

    // spin button visibility goes badly wrong after a settings change
    // this is my best current solution other than subclassing the
    // spin button. Simply calling ShowWindow(SW_HIDE) from here
    // does not work.
    PostMessage(WM_TDC_REFRESHPERCENTSPINVISIBILITY);
}

LRESULT CToDoCtrl::OnRefreshPercentSpinVisibility(WPARAM /*wp*/, LPARAM /*lp*/)
{
    if (!m_ePercentDone.IsWindowVisible())
        m_spinPercent.ShowWindow(SW_HIDE);

    return 0L;
}

LRESULT CToDoCtrl::OnFixupPostDropSelection(WPARAM /*wp*/, LPARAM lp)
{
	if (lp)
		SelectTask(lp, FALSE);

    return 0L;
}

LRESULT CToDoCtrl::OnChangeColour(WPARAM /*wp*/, LPARAM /*lp*/)
{
	UpdateTask(TDCA_COLOR);

	return 0L;
}

BOOL CToDoCtrl::CanUndoLastAction(BOOL bUndo) const 
{ 
	// handle comments field
	if (m_ctrlComments.HasFocus())
		return (m_nCommentsState != CS_CLEAN);

	// Handle edit fields as below
	CWnd* pFocus = GetFocus();

	if (pFocus && CWinClasses::IsClass(*pFocus, WC_EDIT))
	{
		CEdit* pEdit = (CEdit*)pFocus;
		
		if (pEdit->CanUndo() || IsTaskLabelEditing() || (pEdit->GetDlgCtrlID() == IDC_PROJECTNAME))
			return TRUE;
	}

	return m_data.CanUndoLastAction(bUndo); 
}

BOOL CToDoCtrl::UndoLastAction(BOOL bUndo)
{
	// handle comments field
	if (m_ctrlComments.HasFocus())
		return bUndo ? m_ctrlComments.Undo() : m_ctrlComments.Redo();

	// We pass the undo request to the focused edit if:
	// 1. Tree label editing
	// OR
	// 2. Project description editing
	// OR
	// 3. There is something to be undone
	CWnd* pFocus = GetFocus();

	if (pFocus && CWinClasses::IsClass(*pFocus, WC_EDIT))
	{
		CEdit* pEdit = (CEdit*)pFocus;

		if (pEdit->CanUndo() || IsTaskLabelEditing() || (pEdit->GetDlgCtrlID() == IDC_PROJECTNAME))
		{
			pEdit->Undo();
			return FALSE; 
		}
	}

	if (m_data.CanUndoLastAction(bUndo))
	{
 		CWaitCursor cursor;
		CLockUpdates lu(*this);
		HOLD_REDRAW(*this, m_taskTree);
		
		TDCSELECTIONCACHE cache;
		CacheTreeSelection(cache);

		// fix up selection first in case we are about to delete the selected item
		m_taskTree.DeselectAll();

		// get the list of the task IDs that will be undone/redone
		CDWordArray aTaskIDs;
		TDC_UNDOACTIONTYPE nUndoType = m_data.GetLastUndoActionType(bUndo);

		// but not if the result is that the items in question were deleted
		if (!(nUndoType == TDCUAT_DELETE && !bUndo) && 
			!(nUndoType == TDCUAT_ADD && bUndo))
		{
			m_data.GetLastUndoActionTaskIDs(bUndo, aTaskIDs);
		}

		CArrayUndoElements aElms;

		if (m_data.UndoLastAction(bUndo, aElms))
		{
			VERIFY(UndoLastActionItems(aElms));

			m_taskTree.OnUndoRedo(bUndo);

			// restore selection
			if (!aTaskIDs.GetSize() || !m_taskTree.SelectTasks(aTaskIDs))
			{
				if (!RestoreTreeSelection(cache))
					TSH().PrevSelection(FALSE);
			}
			
			// update current selection
			UpdateControls();

			// If the operation just un/redone was an edit then we treat it as such
			if (nUndoType == TDCUAT_EDIT)
 				SetModified(TDCA_ALL, aTaskIDs);
			else
				SetModified(TDCA_UNDO, aTaskIDs);

			return TRUE;
		}
	}

	return FALSE;
}

BOOL CToDoCtrl::UndoLastActionItems(const CArrayUndoElements& aElms)
{
	// restore elements
	int nNumElm = aElms.GetSize();
	
	// now undo
	for (int nElm = 0; nElm < nNumElm; nElm++)
	{
		TDCUNDOELEMENT elm = aElms[nElm];
		
		if (elm.nOp == TDCUEO_EDIT)
		{
			ASSERT(0); // we shouldn't ever have these
		}
		else if (elm.nOp == TDCUEO_DELETE)
		{
			// find tree item and delete it
			// note: DeleteTask on the Parent will already have disposed of the children
			// so we can expect hti to be NULL on occasion. ie don't ASSERT it
			HTREEITEM hti = m_taskTree.GetItem(elm.dwTaskID);
			ASSERT(hti);
			
			if (hti)
			{
				if (m_timeTracking.IsTrackingTask(elm.dwTaskID, FALSE))
					EndTimeTracking(FALSE, FALSE);

				m_taskTree.DeleteItem(hti);
			}
		}
		else if (elm.nOp == TDCUEO_ADD)
		{
			// find parent item and restore task
			HTREEITEM htiParent = m_taskTree.GetItem(elm.dwParentID);
			ASSERT(htiParent || !elm.dwParentID);

			HTREEITEM htiPrevSibling = m_taskTree.GetItem(elm.dwPrevSiblingID);
			ASSERT(htiPrevSibling || !elm.dwPrevSiblingID);
			
			if ((elm.dwParentID && htiParent) || (!elm.dwParentID && !htiParent))
			{
				HTREEITEM htiNew = m_taskTree.InsertItem(elm.dwTaskID, htiParent, htiPrevSibling);
				ASSERT(htiNew);
				
				if (!htiNew)
					return FALSE;
			}
		}
		else if (elm.nOp == TDCUEO_MOVE)
		{
			// move the task back to it original location
			HTREEITEM hti = m_taskTree.GetItem(elm.dwTaskID); // current tree item
			ASSERT(hti);

			HTREEITEM htiDestParent = m_taskTree.GetItem(elm.dwParentID); // original owner
			ASSERT(htiDestParent || !elm.dwParentID);

			HTREEITEM htiDestPrevSibling = m_taskTree.GetItem(elm.dwPrevSiblingID); // original previous sibling
			ASSERT(htiDestPrevSibling || !elm.dwPrevSiblingID);
			
			if ((elm.dwParentID && htiDestParent) || (!elm.dwParentID && !htiDestParent))
			{
				// undo the move
				if (!m_taskTree.MoveItem(hti, htiDestParent, htiDestPrevSibling))
				{
					ASSERT(0);
					return FALSE;
				}
			}
		}
		else
		{
			return FALSE;
		}
	}
	
	return TRUE;
}

LRESULT CToDoCtrl::OnTDCGetTaskReminder(WPARAM wp, LPARAM lp)
{
	UNREFERENCED_PARAMETER(wp);
	ASSERT(lp && ((HWND)wp == m_taskTree.GetSafeHwnd()));

	return (LRESULT)m_reminders.GetTaskReminder(lp);
}

void CToDoCtrl::SetUITheme(const CUIThemeFile& theme)
{
	ASSERT(GetSafeHwnd());
	
	m_theme = theme;
	
	GraphicsMisc::VerifyDeleteObject(m_brUIBack);
	
	if (m_theme.crAppBackLight != CLR_NONE)
		m_brUIBack.CreateSolidBrush(m_theme.crAppBackLight);
	
	m_ctrlComments.SetUITheme(m_theme);
	m_taskTree.SetSplitBarColor(m_theme.crAppBackDark);

	Invalidate();
}

HBRUSH CToDoCtrl::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor) 
{
	HBRUSH hbr = CRuntimeDlg::OnCtlColor(pDC, pWnd, nCtlColor);
	
	if (CThemed::IsAppThemed() && (nCtlColor == CTLCOLOR_STATIC) && CWinClasses::IsClass(*pWnd, WC_STATIC))
	{
		if (m_theme.crAppText != CLR_NONE)
			pDC->SetTextColor(m_theme.crAppText);
		
		if (m_brUIBack.GetSafeHandle())
		{
			pDC->SetBkMode(TRANSPARENT);
			hbr = m_brUIBack;
		}
	}
	
	return hbr;
}

void CToDoCtrl::RedrawReminders()
{ 
	// Called after the app has made a change to a task's reminder
	m_taskTree.OnReminderChange();
}

TDC_ATTRIBUTE CToDoCtrl::GetFocusedControlAttribute() const
{
	HWND hFocus = ::GetFocus();
	UINT nCtrlID = ::GetDlgCtrlID(hFocus);
	TDC_ATTRIBUTE nAttrib = MapCtrlIDToAttribute(nCtrlID);

	// handle edit controls of combos and custom comments plugins
	if (nAttrib == TDCA_NONE)
	{
		if (IsChildOrSame(m_ctrlComments, hFocus))
		{
			nAttrib = TDCA_COMMENTS;
		}
		else if (CWinClasses::IsEditControl(hFocus))
		{
			hFocus = ::GetParent(hFocus);

			if (CWinClasses::IsComboBox(hFocus))
			{
				nCtrlID = ::GetDlgCtrlID(hFocus);
				nAttrib = MapCtrlIDToAttribute(nCtrlID);
			}
		}
	}

	return nAttrib;
}

BOOL CToDoCtrl::CanClearSelectedTaskFocusedAttribute() const
{
	TDC_ATTRIBUTE nAttrib = GetFocusedControlAttribute();

	if (!CanEditSelectedTask(nAttrib))
		return FALSE;

	return CanClearSelectedTaskAttribute(nAttrib);
}

BOOL CToDoCtrl::ClearSelectedTaskFocusedAttribute()
{
	if (!CanClearSelectedTaskFocusedAttribute())
		return FALSE;

	TDC_ATTRIBUTE nAttrib = GetFocusedControlAttribute();

	return ClearSelectedTaskAttribute(nAttrib);
}

BOOL CToDoCtrl::CanClearSelectedTaskAttribute(TDC_ATTRIBUTE nAttrib) const
{
	if (!CanEditSelectedTask(nAttrib))
		return FALSE;

	return ((nAttrib >= TDCA_FIRSTATTRIBUTE && 
			 nAttrib < TDCA_ATTRIBUTECOUNT &&
			 nAttrib != TDCA_TASKNAME && 
			 nAttrib != TDCA_PROJECTNAME) ||
			TDCCUSTOMATTRIBUTEDEFINITION::IsCustomAttribute(nAttrib));
}

BOOL CToDoCtrl::ClearSelectedTaskAttribute(TDC_ATTRIBUTE nAttrib)
{
	switch (nAttrib)
	{
	case TDCA_DONEDATE:		return SetSelectedTaskDate(TDCD_DONE, 0.0);
	case TDCA_DUEDATE:		return SetSelectedTaskDate(TDCD_DUE, 0.0);
	case TDCA_STARTDATE:	return SetSelectedTaskDate(TDCD_START, 0.0);
		
	case TDCA_DONETIME:		return SetSelectedTaskDate(TDCD_DONETIME, 0.0);
	case TDCA_DUETIME:		return SetSelectedTaskDate(TDCD_DUETIME, 0.0);
	case TDCA_STARTTIME:	return SetSelectedTaskDate(TDCD_STARTTIME, 0.0);
		
	case TDCA_PRIORITY:		return SetSelectedTaskPriority(FM_NOPRIORITY);
	case TDCA_RISK:			return SetSelectedTaskRisk(FM_NORISK);
		
	case TDCA_ALLOCTO:		return SetSelectedTaskAllocTo(CStringArray());
	case TDCA_CATEGORY:		return SetSelectedTaskCategories(CStringArray());
	case TDCA_TAGS:			return SetSelectedTaskTags(CStringArray());
	case TDCA_DEPENDENCY:	return SetSelectedTaskDependencies(CStringArray());
	case TDCA_FILEREF:		return SetSelectedTaskFileRefs(CStringArray());
		
	case TDCA_ALLOCBY:		return SetSelectedTaskAllocBy(_T(""));
	case TDCA_STATUS:		return SetSelectedTaskStatus(_T(""));
	case TDCA_VERSION:		return SetSelectedTaskVersion(_T(""));
	case TDCA_EXTERNALID:	return SetSelectedTaskExternalID(_T(""));
		
	case TDCA_PERCENT:		return SetSelectedTaskPercentDone(0);
	case TDCA_FLAG:			return SetSelectedTaskFlag(FALSE);
	case TDCA_LOCK:			return SetSelectedTaskLock(FALSE);
	case TDCA_COLOR:		return SetSelectedTaskColor(0);
	case TDCA_RECURRENCE:	return SetSelectedTaskRecurrence(TDCRECURRENCE());
		
	case TDCA_TIMEEST:		
		{
			// preserve existing units
			TDCTIMEPERIOD time;
			VERIFY(GetSelectedTaskTimeEstimate(time)); 

			time.dAmount = 0.0;
			return SetSelectedTaskTimeEstimate(time);
		}

	case TDCA_TIMESPENT:
		{
			// preserve existing units
			TDCTIMEPERIOD time;
			VERIFY(GetSelectedTaskTimeSpent(time));

			time.dAmount = 0.0;
			return SetSelectedTaskTimeSpent(time);
		}

	case TDCA_COST:
		{ 
			// preserve 'IsRate'
			TDCCOST cost;
			VERIFY(GetSelectedTaskCost(cost));

			cost.dAmount = 0.0;
			return SetSelectedTaskCost(cost);
		}

	// these have no field
	case TDCA_SUBTASKDONE:
	case TDCA_POSITION:
	case TDCA_POSITION_SAMEPARENT:
	case TDCA_POSITION_DIFFERENTPARENT:
	case TDCA_CREATEDBY:
	case TDCA_CREATIONDATE:
	case TDCA_LASTMODDATE:
	case TDCA_LASTMODBY:
	case TDCA_ICON:
		ASSERT(0);
		return FALSE;

	// These cannot be cleared
	case TDCA_ID:
	case TDCA_PARENTID:
	case TDCA_PATH:
		ASSERT(0);
		return FALSE;
	}

	// fall thru to custom attributes
	CString sCustomAttribID = m_aCustomAttribDefs.GetAttributeTypeID(nAttrib);

	if (!sCustomAttribID.IsEmpty())
		return ClearSelectedTaskCustomAttributeData(sCustomAttribID, FALSE);

	// else something we've missed
	ASSERT(0);
	return FALSE;
}

BOOL CToDoCtrl::SelectedTaskIsUnlocked(DWORD dwTaskID) const
{
	if (dwTaskID)
		return (m_taskTree.IsTaskSelected(dwTaskID) && !m_calculator.IsTaskLocked(dwTaskID));

	return !m_taskTree.SelectionHasLocked(FALSE);
}

BOOL CToDoCtrl::CanEditSelectedTask(TDC_ATTRIBUTE nAttrib, DWORD dwTaskID) const 
{ 
	if (IsReadOnly())
		return FALSE;

	switch (nAttrib)
	{
	case TDCA_NONE:
		return FALSE;

	case TDCA_PERCENT:
		if (!SelectedTaskIsUnlocked(dwTaskID))
		{
			return FALSE;
		}
		else if (HasStyle(TDCS_AUTOCALCPERCENTDONE))
		{
			return FALSE;
		}
		else if (HasStyle(TDCS_AVERAGEPERCENTSUBCOMPLETION))
		{
			if (dwTaskID)
			{
				if (!m_taskTree.IsTaskSelected(dwTaskID) || m_data.IsTaskParent(dwTaskID))
					return FALSE;
			}
			else if (m_taskTree.SelectionHasSubtasks())
			{
				return FALSE;
			}
		}
		return TRUE;

	case TDCA_ALLOCBY:		
	case TDCA_ALLOCTO:		
	case TDCA_ANYTEXTATTRIBUTE:		
	case TDCA_CATEGORY:		
	case TDCA_COLOR:		
	case TDCA_COMMENTS:		
	case TDCA_COST:			
	case TDCA_CREATEDBY:	
	case TDCA_CREATIONDATE:	
	case TDCA_DEPENDENCY:	
	case TDCA_DONEDATE:		
	case TDCA_DONETIME:		
	case TDCA_DUEDATE:		
	case TDCA_DUETIME:		
	case TDCA_EXTERNALID:	
	case TDCA_FILEREF:		
	case TDCA_FLAG:			
	case TDCA_ICON:		
	case TDCA_METADATA:
	case TDCA_OFFSETTASK:
	case TDCA_PRIORITY:		
	case TDCA_RECURRENCE:	
	case TDCA_RISK:			
	case TDCA_STATUS:		
	case TDCA_TAGS:			
	case TDCA_TASKNAME:		
	case TDCA_TASKNAMEORCOMMENTS:		
	case TDCA_VERSION:		
		return SelectedTaskIsUnlocked(dwTaskID);

	case TDCA_TIMEEST:
	case TDCA_TIMESPENT:
		if (!SelectedTaskIsUnlocked(dwTaskID))
		{
			return FALSE;
		}
		else if (dwTaskID)
		{
			if (!HasStyle(TDCS_ALLOWPARENTTIMETRACKING) && m_data.IsTaskParent(dwTaskID))
				return FALSE;
		}
		return TRUE;

	case TDCA_STARTDATE:
	case TDCA_STARTTIME:
		if (!SelectedTaskIsUnlocked(dwTaskID))
		{
			return FALSE;
		}
		else if (HasStyle(TDCS_AUTOADJUSTDEPENDENCYDATES) && m_taskTree.SelectionHasDependencies())
		{
			// Ignore tasks with dependencies where their dates 
			// are automatically calculated
			return FALSE;
		}
		else if ((nAttrib == TDCA_STARTTIME) && !SelectedTaskHasDate(TDCD_START))
		{
			// Ignore tasks without a start date set
			return FALSE;
		}
		return TRUE;

	case TDCA_DELETE:
		// Can't delete locked tasks unless they are references
		// Can't delete subtasks if immediate parent is locked
		if (m_taskTree.SelectionHasLocked(FALSE, TRUE) || 
			m_taskTree.SelectionHasLockedParent(TRUE))
		{
			return FALSE;
		}
		return TRUE;

	case TDCA_NEWTASK:
	case TDCA_PASTE:
	case TDCA_PROJECTNAME:
	case TDCA_UNDO:
	case TDCA_CUSTOMATTRIBDEFS:
	case TDCA_POSITION:
	case TDCA_ENCRYPT:
		return TRUE;

	case TDCA_LOCK:
		return GetSelectedCount();

	default:
		if (TDCCUSTOMATTRIBUTEDEFINITION::IsCustomAttribute(nAttrib))
			return SelectedTaskIsUnlocked(dwTaskID);
		break;
	}

	// all else
	ASSERT(0);
	return FALSE;
}

BOOL CToDoCtrl::CopySelectedTaskAttributeData(TDC_ATTRIBUTE nFromAttrib, TDC_ATTRIBUTE nToAttrib)
{
	if (!CanCopyAttributeData(nFromAttrib, nToAttrib))
		return FALSE;

	Flush();

	IMPLEMENT_DATA_UNDO_EDIT(m_data);

	CDWordArray aModTaskIDs;
	POSITION pos = TSH().GetFirstItemPos();

	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);
		
		if (!HandleModResult(dwTaskID, m_data.CopyTaskAttributeValues(dwTaskID, nFromAttrib, nToAttrib), aModTaskIDs))
			return FALSE;
	}

	if (aModTaskIDs.GetSize())
	{
		SetModified(nToAttrib, aModTaskIDs);
		UpdateControls(FALSE);
	}

	return FALSE;
}

BOOL CToDoCtrl::CopySelectedTaskAttributeData(TDC_ATTRIBUTE nFromAttrib, const CString& sToCustomAttribID)
{
	TDCCUSTOMATTRIBUTEDEFINITION attribDefTo;

	if (!m_aCustomAttribDefs.GetAttributeDef(sToCustomAttribID, attribDefTo))
		return FALSE;
	
	if (!CanCopyAttributeData(nFromAttrib, attribDefTo))
		return FALSE;

	Flush();

	IMPLEMENT_DATA_UNDO_EDIT(m_data);

	CDWordArray aModTaskIDs;
	POSITION pos = TSH().GetFirstItemPos();

	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);

		if (!HandleModResult(dwTaskID, m_data.CopyTaskAttributeValues(dwTaskID, nFromAttrib, sToCustomAttribID), aModTaskIDs))
			return FALSE;
	}

	if (aModTaskIDs.GetSize())
	{
		TDC_ATTRIBUTE nAttrib = m_aCustomAttribDefs.GetAttributeID(sToCustomAttribID);
		SetModified(nAttrib, aModTaskIDs);
		
		UpdateControls(FALSE);
	}

	return TRUE;
}

BOOL CToDoCtrl::CopySelectedTaskAttributeData(const CString& sFromCustomAttribID, TDC_ATTRIBUTE nToAttrib)
{
	TDCCUSTOMATTRIBUTEDEFINITION attribDefFrom;

	if (!m_aCustomAttribDefs.GetAttributeDef(sFromCustomAttribID, attribDefFrom))
		return FALSE;

	if (!CanCopyAttributeData(attribDefFrom, nToAttrib))
		return FALSE;

	Flush();

	POSITION pos = TSH().GetFirstItemPos();
	TDC_SET nRes = SET_NOCHANGE;
	CDWordArray aModTaskIDs;

	IMPLEMENT_DATA_UNDO_EDIT(m_data);

	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);

		if (!HandleModResult(dwTaskID, m_data.CopyTaskAttributeValues(dwTaskID, sFromCustomAttribID, nToAttrib), aModTaskIDs))
			return FALSE;
	}

	if (aModTaskIDs.GetSize())
	{
 		SetModified(nToAttrib, aModTaskIDs);
		UpdateControls(FALSE);
	}

	return TRUE;
}

BOOL CToDoCtrl::CopySelectedTaskAttributeData(const CString& sFromCustomAttribID, const CString& sToCustomAttribID)
{
	// Doesn't make sense to copy to self
	if (sToCustomAttribID == sFromCustomAttribID)
	{
		ASSERT(0);
		return FALSE;
	}

	DWORD dwFromType = m_aCustomAttribDefs.GetAttributeDataType(sFromCustomAttribID);
	DWORD dwToType = m_aCustomAttribDefs.GetAttributeDataType(sToCustomAttribID);

	if (dwFromType != dwToType)
		return FALSE;

	Flush();

	POSITION pos = TSH().GetFirstItemPos();
	TDC_SET nRes = SET_NOCHANGE;
	CDWordArray aModTaskIDs;

	IMPLEMENT_DATA_UNDO_EDIT(m_data);

	while (pos)
	{
		DWORD dwTaskID = TSH().GetNextItemData(pos);

		if (!HandleModResult(dwTaskID, m_data.CopyTaskAttributeValues(dwTaskID, sFromCustomAttribID, sToCustomAttribID), aModTaskIDs))
			return FALSE;
	}

	if (aModTaskIDs.GetSize())
	{
		TDC_ATTRIBUTE nAttrib = m_aCustomAttribDefs.GetAttributeID(sToCustomAttribID);
		SetModified(nAttrib, aModTaskIDs);

		UpdateControls(FALSE);
	}

	return TRUE;
}

BOOL CToDoCtrl::CanCopyAttributeData(TDC_ATTRIBUTE nFromAttrib, TDC_ATTRIBUTE nToAttrib)
{
	// Doesn't make sense to copy to self
	if (nFromAttrib == nToAttrib)
	{
		ASSERT(0);
		return FALSE;
	}

	switch (nFromAttrib)
	{
	case TDCA_ALLOCBY:			
	case TDCA_ALLOCTO:			
	case TDCA_CREATEDBY:	
	case TDCA_LASTMODBY:	
		switch (nToAttrib)
		{
		// Note: TDCA_CREATEDBY cannot be copied to
		// Note: TDCA_LASTMODBY cannot be copied to
		case TDCA_ALLOCBY:			
		case TDCA_ALLOCTO:			
			return TRUE;
		}
		break;

	case TDCA_CATEGORY:			
	case TDCA_EXTERNALID:		
	case TDCA_STATUS:			
	case TDCA_TAGS:				
	case TDCA_TASKNAME:			
	case TDCA_VERSION:			
	case TDCA_ICON:				
		switch (nToAttrib)
		{
		case TDCA_CATEGORY:			
		case TDCA_EXTERNALID:		
		case TDCA_STATUS:			
		case TDCA_TAGS:				
		case TDCA_TASKNAME:			
		case TDCA_VERSION:			
		case TDCA_ICON:				
			return TRUE;
		}
		break;

	case TDCA_PRIORITY:			
	case TDCA_RISK:				
		switch (nToAttrib)
		{
		case TDCA_PRIORITY:			
		case TDCA_RISK:				
			return TRUE;
		}
		break;

	case TDCA_CREATIONDATE:		
	case TDCA_DONEDATE:			
	case TDCA_DUEDATE:			
	case TDCA_LASTMODDATE:			
	case TDCA_STARTDATE:		
		switch (nToAttrib)
		{
		// Note: TDCA_CREATIONDATE cannot be copied to
		// Note: TDCA_LASTMOD cannot be copied to
		case TDCA_DONEDATE:			
		case TDCA_DUEDATE:			
		case TDCA_STARTDATE:		
			return TRUE;
		}
		break;

	case TDCA_DONETIME:			
	case TDCA_DUETIME:			
	case TDCA_STARTTIME:		
		switch (nToAttrib)
		{
		case TDCA_DONETIME:			
		case TDCA_DUETIME:			
		case TDCA_STARTTIME:		
			return TRUE;
		}
		break;

	case TDCA_FLAG:				
	case TDCA_LOCK:				
		switch (nToAttrib)
		{
		case TDCA_FLAG:				
		case TDCA_LOCK:				
			return TRUE;
		}
		break;

	case TDCA_TIMEEST:			
	case TDCA_TIMESPENT:		
		switch (nToAttrib)
		{
		case TDCA_TIMEEST:			
		case TDCA_TIMESPENT:		
			return TRUE;
		}
		break;

	}

	return FALSE;
}

BOOL CToDoCtrl::CanCopyAttributeData(TDC_ATTRIBUTE nFromAttrib, const TDCCUSTOMATTRIBUTEDEFINITION& attribDefFrom)
{
	switch (nFromAttrib)
	{
	case TDCA_VERSION:			
	case TDCA_ALLOCBY:			
	case TDCA_CREATEDBY:	
	case TDCA_EXTERNALID:		
	case TDCA_STATUS:			
	case TDCA_TASKNAME:	
	case TDCA_COMMENTS:			
	case TDCA_DEPENDENCY:		
	case TDCA_FILEREF:			
	case TDCA_ICON:				
	case TDCA_LASTMODBY:
		return attribDefFrom.IsDataType(TDCCA_STRING);

	case TDCA_ALLOCTO:			
	case TDCA_CATEGORY:			
	case TDCA_TAGS:				
		return (attribDefFrom.IsDataType(TDCCA_STRING) && attribDefFrom.IsMultiList());

	case TDCA_COLOR:			
	case TDCA_PRIORITY:			
	case TDCA_RISK:				
	case TDCA_POSITION:			
	case TDCA_PERCENT:			
	case TDCA_COST:	
		return (attribDefFrom.IsDataType(TDCCA_INTEGER) || attribDefFrom.IsDataType(TDCCA_DOUBLE));

	case TDCA_CREATIONDATE:		
	case TDCA_DONEDATE:			
	case TDCA_DUEDATE:			
	case TDCA_LASTMODDATE:			
	case TDCA_STARTDATE:		
	case TDCA_DONETIME:			
	case TDCA_DUETIME:			
	case TDCA_STARTTIME:		
		return attribDefFrom.IsDataType(TDCCA_DATE);

	case TDCA_FLAG:				
	case TDCA_LOCK:				
		return attribDefFrom.IsDataType(TDCCA_BOOL);

	case TDCA_TIMEEST:			
	case TDCA_TIMESPENT:		
		return attribDefFrom.IsDataType(TDCCA_TIMEPERIOD);
	}

	return FALSE;
}

BOOL CToDoCtrl::CanCopyAttributeData(const TDCCUSTOMATTRIBUTEDEFINITION& attribDefFrom, TDC_ATTRIBUTE nToAttrib)
{
	switch (attribDefFrom.GetDataType())
	{
	case TDCCA_STRING:
		switch(nToAttrib)
		{
		case TDCA_EXTERNALID:		
		case TDCA_STATUS:			
		case TDCA_TASKNAME:			
		case TDCA_VERSION:			
		case TDCA_ALLOCBY:			
		case TDCA_CREATEDBY:	
		case TDCA_COMMENTS:			
		case TDCA_DEPENDENCY:		
		case TDCA_FILEREF:			
		case TDCA_ALLOCTO:			
		case TDCA_CATEGORY:			
		case TDCA_TAGS:				
		case TDCA_ICON:				
			return TRUE;
		}
		break;

	case TDCCA_INTEGER:
		switch(nToAttrib)
		{
		case TDCA_COLOR:			
		case TDCA_PRIORITY:			
		case TDCA_RISK:				
		case TDCA_PERCENT:			
			return TRUE;
		}
		break;

	case TDCCA_DATE:
		switch(nToAttrib)
		{
		case TDCA_DONEDATE:			
		case TDCA_DUEDATE:			
		case TDCA_STARTDATE:		
		case TDCA_DONETIME:			
		case TDCA_DUETIME:			
		case TDCA_STARTTIME:		
			return TRUE;
		}
		break;

	case TDCCA_DOUBLE:
		return (nToAttrib == TDCA_COST);

	case TDCCA_BOOL:
		switch(nToAttrib)
		{
		case TDCA_FLAG:				
		case TDCA_LOCK:				
			return TRUE;
		}
		break;

	case TDCCA_TIMEPERIOD:
		switch(nToAttrib)
		{
		case TDCA_TIMEEST:			
		case TDCA_TIMESPENT:		
			return TRUE;
		}
		break;
	}

	return FALSE;
}

BOOL CToDoCtrl::SaveTaskViewToImage(const CString& sFilePath) 
{ 
	CBitmap bmImage;

	if (m_taskTree.SaveToImage(bmImage))
		return (CGdiPlusBitmap(bmImage).SaveAsFile(sFilePath) != FALSE);

	// else
	return FALSE;
}

