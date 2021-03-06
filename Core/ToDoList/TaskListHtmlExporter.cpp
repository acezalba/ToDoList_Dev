// TaskFileHtmlExporter.cpp: implementation of the CTaskListHtmlExporter class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "TasklistHtmlExporter.h"
#include "tdlrecurringtaskedit.h"

#include "..\shared\xmlfile.h"
#include "..\shared\filemisc.h"
#include "..\shared\graphicsmisc.h"

#include "..\3rdparty\stdiofileex.h"

#include "..\Interfaces\Preferences.h"
#include "..\Interfaces\TasklistSchemaDef.h"

#include <locale.h>
#include <Shlwapi.h>

//////////////////////////////////////////////////////////////////////

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

static LPCTSTR COMMENTS_DONECOLOR = _T("#808080");
static LPCTSTR DOCTYPE = _T("<!DOCTYPE HTML PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\" \"http://www.w3.org/TR/html4/loose.dtd\">\n\n");
static LPCTSTR TAB = _T("&nbsp;&nbsp;&nbsp;&nbsp;");
static LPCTSTR SPACE = _T("&nbsp;");

CTaskListHtmlExporter::CTaskListHtmlExporter() 
	: 
	STRIKETHRUDONE(TRUE), 
	EXPORTSTYLE(TDLPDS_WRAP),
	ROOT(TRUE), 
	COMMENTSPERCENTWIDTH(30)
{
	// override base class ENDL
	ENDL = "\n";
	LISTSEPARATOR = (Misc::GetListSeparator() + ' '); // allows wrapping
}

CTaskListHtmlExporter::~CTaskListHtmlExporter()
{
	
}

IIMPORTEXPORT_RESULT CTaskListHtmlExporter::ExportOutput(LPCTSTR szDestFilePath, const CString& sOutput) const
{
	CString sHtmlOutput;
	
	sHtmlOutput += DOCTYPE;
	sHtmlOutput += _T("<html>\n<head>\n");
	sHtmlOutput += _T("<style type=\"text/css\">\n");
	sHtmlOutput += _T("@media print { thead {display: table-header-group;} }\n");
	sHtmlOutput += _T("body { line-height: 1.0; }\n");
	sHtmlOutput += _T("table { border-collapse: collapse; }\n");
	sHtmlOutput += _T("</style>\n");
	sHtmlOutput += _T("<meta http-equiv=\"X-UA-Compatible\" content=\"IE=edge\">\n");
	sHtmlOutput += CHARSET;
	sHtmlOutput += _T("</head>\n<body>\n");
	sHtmlOutput += sOutput;
	sHtmlOutput += _T("</body>\n</html>\n");

	return CTaskListExporterBase::ExportOutput(szDestFilePath, sHtmlOutput);
}

bool CTaskListHtmlExporter::InitConsts(const ITASKLISTBASE* pTasks, LPCTSTR szDestFilePath, bool bSilent, 
									   IPreferences* pPrefs, LPCTSTR szKey)
{
	// base consts
	if (!CTaskListExporterBase::InitConsts(pTasks, szDestFilePath, bSilent, pPrefs, szKey))
		return false;

	// then ours
	szKey = _T("Preferences");
		
	CString sFont = pPrefs->GetProfileString(szKey, _T("HTMLFont"), _T("Verdana"));
	int nFontSize = pPrefs->GetProfileInt(szKey, _T("HtmlFontSize"), 2);
	
	DEFAULTFONT.Format(_T("<font face='%s' size='%d'>"), sFont, nFontSize);
	HTMLNOTES.Empty();
	
	if (pPrefs->GetProfileInt(szKey, _T("ExportSpaceForNotes"), FALSE))
	{
		int nLine = pPrefs->GetProfileInt(szKey, _T("LineSpaces"), 8);
		
		if (nLine > 0)
		{
			HTMLNOTES = "<pre>";
			
			while (nLine--)
				HTMLNOTES += ENDL;
			
			HTMLNOTES += "</pre>";
		}
	}
	
	STRIKETHRUDONE = pPrefs->GetProfileInt(szKey, _T("StrikethroughDone"), TRUE);
	EXPORTSTYLE = ValidateExportStyle(_ttoi(pTasks->GetMetaData(TDL_EXPORTSTYLE)));

	INDENT.Empty();

	if (pPrefs->GetProfileInt(szKey, _T("UseSpaceIndents"), TRUE))
	{
		int nSpace = pPrefs->GetProfileInt(szKey, _T("TextIndent"), 2);

		while (nSpace--)
			INDENT += SPACE;
	}
	else
	{
		INDENT = TAB;
	}
	
	// charset
#ifdef _UNICODE
	CString sCS = "UTF-16"; 
#else
	CString sCS = pTasks->GetHtmlCharSet();
#endif
	
	if (!sCS.IsEmpty())
		CHARSET.Format(_T("<meta http-equiv=\"content-type\" content=\"text/html; charset=%s\">\n"), sCS);

	if (WantAttribute(TDCA_COMMENTS))
	{
		COMMENTSPERCENTWIDTH = 30; // minimum

		switch (ARRATTRIBUTES.GetSize())
		{
		case 2: // title & comments
			COMMENTSPERCENTWIDTH = 60;
			break;

		case 3:
		case 4:
		case 5:
		case 6:
			COMMENTSPERCENTWIDTH = 50;
			break;

		case 7:
		case 8:
		case 9:
		case 10:
			COMMENTSPERCENTWIDTH = 40;
			break;
		}
	}


	return true;
}

TDLPD_STYLE CTaskListHtmlExporter::ValidateExportStyle(int nStyle)
{
	switch (nStyle)
	{
	case TDLPDS_WRAP:
	case TDLPDS_TABLE:
	case TDLPDS_PARA:
		return (TDLPD_STYLE)nStyle;
	}

	// all else
	return TDLPDS_WRAP;
}

IIMPORTEXPORT_RESULT CTaskListHtmlExporter::Export(const ITaskList* pSrcTaskFile, LPCTSTR szDestFilePath, bool bSilent, IPreferences* pPrefs, LPCTSTR szKey)
{
	return CTaskListExporterBase::Export(pSrcTaskFile, szDestFilePath, bSilent, pPrefs, szKey);
}

IIMPORTEXPORT_RESULT CTaskListHtmlExporter::Export(const IMultiTaskList* pSrcTaskFile, LPCTSTR szDestFilePath, bool bSilent, IPreferences* pPrefs, LPCTSTR szKey)
{
	return CTaskListExporterBase::Export(pSrcTaskFile, szDestFilePath, bSilent, pPrefs, szKey);
}

CString CTaskListHtmlExporter::FormatTitle(const ITASKLISTBASE* pTasks) const
{
	CString sTitleBlock;
			
	// title and date
	CString sTitle = pTasks->GetReportTitle();
	CString sDate = pTasks->GetReportDate();
			
	if (!sTitle.IsEmpty())
	{
		CString sProjTitle;
		sProjTitle.Format(_T("<h2>%s</h2>%s<p/>"), sTitle, sDate);
		
		sTitleBlock += DEFAULTFONT;
		sTitleBlock += sProjTitle;
	}
	else if (!sDate.IsEmpty())
	{
		sTitleBlock += DEFAULTFONT;
		sTitleBlock += sDate;
		sTitleBlock += _T("<p/>");
	}
	
	if (EXPORTSTYLE == TDLPDS_TABLE)
	{
		sTitleBlock += _T("<table border=\"1\">\n");
	}

	return sTitleBlock;
}

CString CTaskListHtmlExporter::FormatHeader(const ITASKLISTBASE* pTasks) const
{
	CString sHeader = CTaskListExporterBase::FormatHeader(pTasks);

	if ((EXPORTSTYLE == TDLPDS_TABLE) && !sHeader.IsEmpty())
	{
		sHeader = _T("<thead><tr>") + sHeader + _T("</tr></thead>");
	}

	return sHeader;
}

CString CTaskListHtmlExporter::FormatHeaderItem(TDC_ATTRIBUTE nAttrib, const CString& sAttribLabel) const
{
	CString sItem;

	if (EXPORTSTYLE == TDLPDS_TABLE)
	{
		if (nAttrib == TDCA_COMMENTS)
			sItem.Format(_T("<th width=\"%d%%\">%s</th>"), COMMENTSPERCENTWIDTH, sAttribLabel);
		else
			sItem.Format(_T("<th>%s</th>"), sAttribLabel);
	}

	return sItem;
}

CString CTaskListHtmlExporter::ExportTask(const ITASKLISTBASE* pTasks, HTASKITEM hTask, int nDepth) const
{
	// snapshot depth so we only embolden top-level tasks
	ROOT = (nDepth <= 1);

	CString sTask = CTaskListExporterBase::ExportTask(pTasks, hTask, nDepth);

	switch (EXPORTSTYLE)
	{
	case TDLPDS_WRAP:
	case TDLPDS_PARA:
		// figure out indent
		if (pTasks->IsAttributeAvailable(TDCA_POSITION))
		{
			if (nDepth > 1)
			{
				sTask = _T("<blockquote>") + sTask;

				if (!pTasks->GetFirstTask(hTask)) // no sub-tasks
					sTask += _T("</blockquote>");
			}
		}
		else
		{
			if (!ROOT)
				sTask = _T("<li>") + sTask;
			
			// handle subtasks
			if (pTasks->GetFirstTask(hTask)) // at least one sub-task
				sTask += _T("<ul>");
		}

		// wrapped tasks without subtasks
		if ((EXPORTSTYLE == TDLPDS_WRAP) && !pTasks->GetFirstTask(hTask))
			sTask += _T("<br>");

		sTask += _T("<br>");
		break;
		
	case TDLPDS_TABLE:
		sTask = _T("<tr>") + sTask + _T("</tr>");
		break;

	default:
		ASSERT(0);
	}

	return sTask;
}

CString CTaskListHtmlExporter::ExportSubtasks(const ITASKLISTBASE* pTasks, HTASKITEM hTask, int nDepth) const
{
	CString sSubtasks = CTaskListExporterBase::ExportSubtasks(pTasks, hTask, nDepth);

	if (!sSubtasks.IsEmpty()) // at least one sub-task
	{
		if (EXPORTSTYLE != TDLPDS_TABLE)
		{
			if (pTasks->IsAttributeAvailable(TDCA_POSITION))
				sSubtasks += _T("</blockquote>");
			else
				sSubtasks += _T("</ul>");
		}
	}

	if (nDepth == 0) // root
	{
		ASSERT(hTask == NULL);

		if (EXPORTSTYLE == TDLPDS_TABLE)
		{
			sSubtasks += _T("</table>\n");
		}
	}

	return sSubtasks;
}

// virtual override
CString CTaskListHtmlExporter::FormatAttribute(TDC_ATTRIBUTE nAttrib, const CString& sAttribLabel, const CString& sValue) const
{
	return FormatAttribute(nAttrib, sAttribLabel, sValue, TRUE);
}

// non-virtual helper
CString CTaskListHtmlExporter::FormatAttribute(TDC_ATTRIBUTE nAttrib, const CString& sAttribLabel, const CString& sValue, BOOL bEncodeVal) const
{
	CString sFmtAttrib;
	CString sAttribVal(sValue);

	if (bEncodeVal)
		TXT2XML(sAttribVal);

	// Must process empty values in table format
	if (!sAttribVal.IsEmpty() || (EXPORTSTYLE == TDLPDS_TABLE))
	{
		switch (nAttrib)
		{
		case TDCA_POSITION:
			sFmtAttrib.Format(_T("%s%s"), sAttribVal, SPACE);
			break;

		case TDCA_TASKNAME:
			{
				// embolden top-level tasks only
				if (ROOT)
					sFmtAttrib += _T("<b>") + sAttribVal + _T("</b>");
				else
					sFmtAttrib += sAttribVal;

				if (EXPORTSTYLE == TDLPDS_PARA)
					sFmtAttrib += _T("<br>");
			}
			break;

		// all else
		default:
			{
				switch (EXPORTSTYLE)
				{
				case TDLPDS_WRAP:
					if (nAttrib == TDCA_COMMENTS)
					{
						sFmtAttrib = ENDL + sAttribVal;
					}
					else
					{
						if (sAttribLabel.IsEmpty())
							sFmtAttrib.Format(_T(" | %s"), sAttribVal);
						else
							sFmtAttrib.Format(_T(" | %s: %s"), sAttribLabel, sAttribVal);
					}				
					break;
					
				case TDLPDS_TABLE:
					// special case: custom attrib
					if (TDCCUSTOMATTRIBUTEDEFINITION::IsCustomAttribute(nAttrib))
						sFmtAttrib = FormatTableCell(sAttribVal);
					else
						sFmtAttrib = sAttribVal;
					break;
					
				case TDLPDS_PARA:
					if (sAttribLabel.IsEmpty())
						sFmtAttrib = sAttribVal;
					else
						sFmtAttrib.Format(_T("%s: %s"), sAttribLabel, sAttribVal);

					if (nAttrib != TDCA_COMMENTS)
						sFmtAttrib += _T("<br>");
					break;
				}
			}
			break;
		}
	}

	return sFmtAttrib;
}

CString CTaskListHtmlExporter::FormatAttribute(const ITASKLISTBASE* pTasks, HTASKITEM hTask, int nDepth, 
											   TDC_ATTRIBUTE nAttrib, const CString& sAttribLabel) const
{
	// base processing
	CString sItem = CTaskListExporterBase::FormatAttribute(pTasks, hTask, nDepth, nAttrib, sAttribLabel);

	// extra processing
	CString sTextColor = ((ITaskList*)pTasks)->GetTaskAttribute(hTask, TDL_TASKTEXTWEBCOLOR);
	CString sBackColor = ((ITaskList*)pTasks)->GetTaskAttribute(hTask, TDL_TASKBACKWEBCOLOR);
	BOOL bColor = TRUE;
	BOOL bStrikeThru = (STRIKETHRUDONE && pTasks->IsTaskDone(hTask));
	BOOL bBlockQuote = FALSE;
	
	switch (nAttrib)
	{
	case TDCA_POSITION:
	case TDCA_TASKNAME:
		// Indent subtasks in table view only
		if (EXPORTSTYLE == TDLPDS_TABLE)
		{
			if ((nAttrib == TDCA_POSITION) || !pTasks->IsAttributeAvailable(TDCA_POSITION))
			{
				while (--nDepth)
					sItem = (INDENT + sItem);
			}
		}
		break;

	case TDCA_PARENTID:
		// ignore if not set
		if (pTasks->GetTaskParentID(hTask) == 0)
			sItem.Empty();
		break;
		
	case TDCA_PRIORITY:
		{
			int nPriority = pTasks->GetTaskPriority(hTask, TRUE);
			CString sPriority;

			if (nPriority >= 0)
			{
				CString sPriorityCol = pTasks->GetTaskPriorityWebColor(hTask);
				sPriority.Format(_T("<font color='%s'>%d</font>"), sPriorityCol, nPriority);

				sItem = FormatAttribute(nAttrib, sAttribLabel, sPriority, FALSE); // FALSE = Don't encode
			}
		}
		break;
		
	case TDCA_FILEREF:
		if (!sItem.IsEmpty())
		{
			// do it over creating a link for each file ref
			CString sFileRefs;
			int nNumFileRefs = pTasks->GetTaskFileLinkCount(hTask);

			for (int nFile = 0; nFile < nNumFileRefs; nFile++) 
			{ 
				CString sFilePath = pTasks->GetTaskFileLink(hTask, nFile), sFileName;

				if (PathIsURL(sFilePath))
				{
					sFileName = sFilePath;
				}
				else
				{
					sFileName = FileMisc::GetFileNameFromPath(sFilePath);

					// handle the absence of a filename
					if (sFileName.IsEmpty())
						sFileName = sFilePath;
				}

				CString sFileRef;
				sFileRef.Format(_T("<a href=\"%s\">%s</a>"), sFilePath, sFileName);

				if (!sFileRefs.IsEmpty())
					sFileRefs += ' ';

				sFileRefs += sFileRef;
			} 

			sItem = FormatAttribute(nAttrib, sAttribLabel, sFileRefs, FALSE); // FALSE = Don't encode
			bColor = FALSE;
		}
		break;
		
	case TDCA_COMMENTS:
		if (pTasks->TaskHasAttribute(hTask, TDCA_HTMLCOMMENTS))
		{
			sItem = pTasks->GetTaskAttribute(hTask, TDCA_HTMLCOMMENTS);
			sItem.TrimRight();
			
			// note: we reset the font after the comments because the font
			// face and size may well have been changed
			if (!sItem.IsEmpty())
				sItem += DEFAULTFONT;
			
			bColor = FALSE;
			bStrikeThru = FALSE;
			bBlockQuote = (EXPORTSTYLE != TDLPDS_TABLE);
		}
		else if (!sItem.IsEmpty())
		{
			// Note: Comments have already been HTML encoded

			// replace carriage returns with <br>
			sItem.Replace(ENDL, _T("<br>"));
			
			// replace tab characters with multiple &nbsp;
			sItem.Replace(_T("\t"), TAB);
			
			sTextColor = pTasks->IsTaskDone(hTask) ? COMMENTS_DONECOLOR : _T("#606060");
			sBackColor.Empty();
			bStrikeThru = FALSE;
			bBlockQuote = (EXPORTSTYLE != TDLPDS_TABLE);
		}
		break;
	}
	
	if (!sItem.IsEmpty())
	{
		if (bStrikeThru)
		{
			CString sStrikeThru;
			sStrikeThru.Format(_T("<s>%s</s>"), sItem);
			sItem = sStrikeThru;
		}
		
		if (bColor)
		{
			CString sColored;

			if (sBackColor.IsEmpty())
				sColored.Format(_T("<span style=\"color:%s\">%s</span>"), sTextColor, sItem);
			else
				sColored.Format(_T("<span style=\"color:%s;background-color:%s;\">%s</span>"), sTextColor, sBackColor, sItem);

			sItem = sColored;
		}
		
		if (bBlockQuote)
		{
			CString sBlockQuote;
			sBlockQuote.Format(_T("<blockquote>%s</blockquote>"), sItem);
			sItem = sBlockQuote;
		}
	}

	// we've already handled custom attrib above
	if ((EXPORTSTYLE == TDLPDS_TABLE) && !TDCCUSTOMATTRIBUTEDEFINITION::IsCustomAttribute(nAttrib))
	{
		if (sItem.IsEmpty())
			sItem = SPACE;

		sItem = FormatTableCell(sItem);
	}

    return sItem;
}

CString CTaskListHtmlExporter::FormatTableCell(const CString& sValue)
{
	return (_T("<td valign=\"top\">") + sValue + _T("</td>"));
}
