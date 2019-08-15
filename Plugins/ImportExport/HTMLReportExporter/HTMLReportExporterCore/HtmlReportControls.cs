﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Windows.Forms;
using System.Drawing;
using System.Diagnostics;
using System.Reflection;
using System.Web.UI;

using MSDN.Html.Editor;

using Abstractspoon.Tdl.PluginHelpers;
using Abstractspoon.Tdl.PluginHelpers.ColorUtil;

namespace HTMLReportExporter
{
	partial class TableHeaderRowOptionCombobox : ComboBox
	{
		private class Item : Tuple<String, TaskTemplate.Layout.TableHeaderRowType>
		{
			public Item(String text, TaskTemplate.Layout.TableHeaderRowType option) : base(text, option) { }
			public override String ToString() { return Item1; }
		}

		public TableHeaderRowOptionCombobox() {}

		public void Initialise(Translator trans)
		{
			Items.Add(new Item(trans.Translate("Auto-generate header row"),  TaskTemplate.Layout.TableHeaderRowType.AutoGenerate));
			Items.Add(new Item(trans.Translate("First row is header row"),   TaskTemplate.Layout.TableHeaderRowType.FirstRow));
			Items.Add(new Item(trans.Translate("No header row is required"), TaskTemplate.Layout.TableHeaderRowType.NotRequired));

			SelectedIndex = 0;
		}

		public TaskTemplate.Layout.TableHeaderRowType SelectedOption
		{
			get
			{
				if (SelectedItem != null)
					return ((Item)SelectedItem).Item2;

				// else
				return TaskTemplate.Layout.TableHeaderRowType.NotRequired;
			}

			set
			{
				foreach (var item in Items)
				{
					if (((Item)item).Item2 == value)
					{
						SelectedItem = item;
						break;
					}
				}
			}
		}
	}
	
	partial class HtmlReportControlBase : HtmlEditorControlEx
	{
		private ToolStripDropDownButton m_ToolStripAttributeMenu = null;
		private Color m_DefaultBackColor = Color.Transparent;
		private String m_DefaultBackImage = String.Empty;

		// ---------------------------------------------------------------

		public new event EventHandler GotFocus;

		// ---------------------------------------------------------------

		protected HtmlReportControlBase() : base(null, null)
		{
			InitializeComponentEx();
		}

		public new void SetControlFont(Font font)
		{
			base.SetControlFont(font);

			this.ToolBar.Font = m_ControlsFont;
		}

		public new void SetTranslator(Translator trans)
		{
			base.SetTranslator(trans);

			m_Trans.Translate(ContextMenu.Items);
			m_Trans.Translate(m_ToolStripAttributeMenu.DropDownItems);

			Toolbars.Sort(m_ToolStripAttributeMenu.DropDownItems);

			// Toolbar translation handled by parent
		}

		public new Color DefaultBackColor
		{
			get { return m_DefaultBackColor; }
			set { m_DefaultBackColor = value; UpdateBackground(); }
		}

		public bool HasDefaultBackColor
		{
			get { return !DrawingColor.IsTransparent(m_DefaultBackColor, true); }
		}
		
		public String DefaultBackImage
		{
			get { return m_DefaultBackImage; }
			set { m_DefaultBackImage = value; UpdateBackground(); }
		}

		protected virtual void UpdateBackground()
		{
			this.BodyBackImage = DefaultBackImage;
			this.BodyBackColor = DefaultBackColor;
		}

		public Color ToolbarBackColor
		{
			get { return ToolBar.BackColor; }
			set { ToolBar.BackColor = value; }
		}

		protected ToolStripDropDownButton ToolStripAttributeMenu
		{
			get { return m_ToolStripAttributeMenu; }
		}

		private new void InitializeComponentEx()
		{
			base.InitializeComponentEx();

			this.ToolbarDock = DockStyle.Top;
			this.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right | AnchorStyles.Bottom;
			this.InnerText = "";
			this.TabIndex = 26;
			this.BorderSize = 0;
			this.ContentMargin = 8;
			this.ParagraphSpacing = 0;

			this.WebBrowser.Document.AttachEventHandler("onfocusout", OnLostFocus);
			this.WebBrowser.Document.AttachEventHandler("onfocusin", OnGotFocus);

			InitialiseFeatures();

			m_ToolStripAttributeMenu = new ToolStripDropDownButton();
			InitialiseToolbar();
		}

		public new void SetBodyFont(string fontName, int htmlSize)
		{
			// Convert size to ems because it gives us greater granularity
			int pointSize = (int)MSDN.Html.Editor.HtmlFontConversion.PointsFromHtml(htmlSize);

			base.SetBodyFont(fontName, pointSize);
		}

		public void SetActive()
		{
			FixupControlPositions();

			WebBrowser.Document.Focus();
		}

		virtual protected void InitialiseFeatures()
		{
			// remove whole 'Document' submenu
			base.InitialiseFeatures(true);

			// extras
			CommandHandling.HideCommand("toolstripEnableEditing", ToolBar.Items);
			//CommandHandling.HideCommand("toolstripInsertLink", ToolBar.Items);
			//CommandHandling.HideCommand("contextInsertLink", ContextMenu.Items);

			//CommandHandling.HideCommand("", ToolBar.Items);
			//CommandHandling.HideCommand("", ToolBar.Items);
			//CommandHandling.HideCommand("", ToolBar.Items);
		}

		virtual protected void InitialiseToolbar()
		{
			InitialiseToolbarAttributeMenu();

			if ((m_ToolStripAttributeMenu != null) && (m_ToolStripAttributeMenu.DropDownItems.Count != 0))
			{
				// Menu items appear one pixel too high when placed in a toolbar
				m_ToolStripAttributeMenu.Margin = new Padding(0, 1, 0, 0);

				foreach (ToolStripItem item in m_ToolStripAttributeMenu.DropDownItems)
					item.Click += new System.EventHandler(this.OnAttributeMenuClick);

				ToolBar.Items.Add(m_ToolStripAttributeMenu);
			}

			base.SetUITheme(new UITheme());
		}

		virtual protected void InitialiseToolbarAttributeMenu()
		{
			// For derived classes
		}

		protected void OnAttributeMenuClick(object sender, EventArgs args)
		{
			HandleAttributeMenuClick(sender as ToolStripMenuItem);
		}

		virtual protected void HandleAttributeMenuClick(ToolStripMenuItem menuItem)
		{
			// For derived classes
		}

		protected override void PreShowDialog(Form dialog)
		{
			base.PreShowDialog(dialog, HTMLReportExporter.Properties.Resources.HTMLReporter);

			var htmlForm = (dialog as EditHtmlForm);

			if (htmlForm != null)
			{
				htmlForm.AddSyntaxPattern(@"\$\(([^)]+)\)", Color.Black, Color.LightGray, FontStyle.Bold);
				htmlForm.RefreshHighlighting();
			}
		}

		public new bool Focused
		{
			get { return (base.Focused || ToolBar.Focused || WebBrowser.Focused); }
		}

		private void OnGotFocus(object sender, EventArgs e)
		{
			if (GotFocus != null)
				GotFocus(this, new EventArgs());
		}

		private void OnLostFocus(object sender, EventArgs e)
		{
			// eat this if the toolbar or us got focused
			if (Focused)
				Focus(); // sets it back to web browser
		}
	}

	/////////////////////////////////////////////////////////////////////

	partial class HtmlReportHeaderFooterControl : HtmlReportControlBase
	{
		private System.Windows.Forms.ToolStripSeparator toolStripSeparator99;
		private System.Windows.Forms.ToolStripButton toolstripBackColor;
		private System.Windows.Forms.ToolStripButton toolstripClearBackColor;

		private Color m_BackColor = Color.Transparent;

		// -----------------------------------------------------

		public HtmlReportHeaderFooterControl()
		{
		}

		override protected void InitialiseFeatures()
		{
			base.InitialiseFeatures();
		}

		override protected void InitialiseToolbar()
		{
			base.InitialiseToolbar();
			
			// Add background colour buttons to toolbar
			int index = ToolBar.Items.IndexOfKey("toolstripTextColor");

			this.toolStripSeparator99 = new System.Windows.Forms.ToolStripSeparator();
			ToolBar.Items.Insert((index + 1), this.toolStripSeparator99);

			this.toolstripBackColor = new System.Windows.Forms.ToolStripButton();
			this.toolstripBackColor.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Image;
			this.toolstripBackColor.Name = "toolstripBackColor";
			this.toolstripBackColor.Tag = "BackColor";
			this.toolstripBackColor.ToolTipText = "Set Background Color";
			this.toolstripBackColor.Click += new System.EventHandler(OnBackColorClick);

			var assembly = Assembly.GetExecutingAssembly();
			var image = new Bitmap(assembly.GetManifestResourceStream("HTMLReportExporter.Resources.ToolbarBackColor.bmp"));
			image.MakeTransparent(Color.Magenta);
			this.toolstripBackColor.Image = image;
			
			this.toolstripBackColor.Size = this.ToolBar.Items[index].Size;

			ToolBar.Items.Insert((index + 2), this.toolstripBackColor);

			this.toolstripClearBackColor = new System.Windows.Forms.ToolStripButton();

			this.toolstripClearBackColor.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Image;
			this.toolstripClearBackColor.Name = "toolstripClearBackColor";
			this.toolstripClearBackColor.Tag = "ClearBackColor";
			this.toolstripClearBackColor.ToolTipText = "Clear Background Color";
			this.toolstripClearBackColor.Click += new System.EventHandler(OnClearBackColorClick);

			var image2 = new Bitmap(assembly.GetManifestResourceStream("HTMLReportExporter.Resources.ToolbarBackColorClear.bmp"));
			image2.MakeTransparent(Color.Magenta);
			this.toolstripClearBackColor.Image = image2;

			this.toolstripClearBackColor.Size = this.toolstripBackColor.Size;

			ToolBar.Items.Insert((index + 3), this.toolstripClearBackColor);
		}

		override protected void InitialiseToolbarAttributeMenu()
		{
			base.InitialiseToolbarAttributeMenu();

			ToolStripAttributeMenu.DropDownItems.Add(new ToolStripMenuItem("Report Title") { Name = "$(reportTitle)" });
			ToolStripAttributeMenu.DropDownItems.Add(new ToolStripMenuItem("Report Date") { Name = "$(reportDate)" });

			ToolStripAttributeMenu.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Text;
			ToolStripAttributeMenu.Text = "Report Attributes";
		}

		public new Color BackColor
		{
			get { return m_BackColor; }
			set { m_BackColor = value; UpdateBackground(); }
		}

		private bool HasBackColor
		{
			get { return !DrawingColor.IsTransparent(BackColor, false); }
		}

		protected override void UpdateBackground()
		{
			// because Image overrides colour but we want the reverse
			// we selectively set the background image if there is a color
			if (HasBackColor)
			{
				BodyBackColor = BackColor;
				BodyBackImage = "";
			}
			else if (HasDefaultBackColor)
			{
				BodyBackColor = DefaultBackColor;
				BodyBackImage = "";
			}
			else
			{
				BodyBackColor = Color.Transparent;
				BodyBackImage = DefaultBackImage;
			}

			UpdateToolbar();
		}

		private void UpdateToolbar()
		{
			this.toolstripClearBackColor.Enabled = HasBackColor;
		}

		override protected void HandleAttributeMenuClick(ToolStripMenuItem menuItem)
		{
			var selText = GetTextRange();

			if (selText != null)
				selText.text = menuItem.Name;
		}

		private void OnBackColorClick(object sender, EventArgs e)
		{
			using (ColorDialog colorDialog = new ColorDialog())
			{
				colorDialog.AnyColor = true;
				colorDialog.SolidColorOnly = true;
				colorDialog.AllowFullOpen = true;
				colorDialog.Color = (HasBackColor ? BackColor : Color.White);
				//colorDialog.CustomColors = _customColors;

				if (colorDialog.ShowDialog(/*this.ParentForm*/) == DialogResult.OK)
				{
					//_customColors = colorDialog.CustomColors;
					BackColor = colorDialog.Color;
				}
			}
		}

		private void OnClearBackColorClick(object sender, EventArgs e)
		{
			BackColor = Color.Transparent;
		}
	}

	/////////////////////////////////////////////////////////////////////

	partial class HtmlReportHeaderControl : HtmlReportHeaderFooterControl
	{
		override protected void InitialiseFeatures()
		{
			base.InitialiseFeatures();

		}
	}

	partial class HtmlReportFooterControl : HtmlReportHeaderFooterControl
	{
		override protected void InitialiseFeatures()
		{
			base.InitialiseFeatures();

		}
	}

	/////////////////////////////////////////////////////////////////////

	partial class HtmlReportTitleControl : HtmlReportControlBase
	{
		override protected void InitialiseFeatures()
		{
			base.InitialiseFeatures();

		}

		override protected void InitialiseToolbarAttributeMenu()
		{
			base.InitialiseToolbarAttributeMenu();

			ToolStripAttributeMenu.DropDownItems.Add(new ToolStripMenuItem("Report Title") { Name = "$(reportTitle)" });
			ToolStripAttributeMenu.DropDownItems.Add(new ToolStripMenuItem("Report Date") { Name = "$(reportDate)" });

			ToolStripAttributeMenu.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Text;
			ToolStripAttributeMenu.Text = "Report Attributes";
		}

		override protected void HandleAttributeMenuClick(ToolStripMenuItem menuItem)
		{
			var selText = GetTextRange();

			if (selText != null)
				selText.text = menuItem.Name;
		}

	}

	/////////////////////////////////////////////////////////////////////

	partial class HtmlReportTaskFormatControl : HtmlReportControlBase
	{
		override protected void InitialiseFeatures()
		{
			base.InitialiseFeatures();
		}

		override protected void InitialiseToolbarAttributeMenu()
		{
			base.InitialiseToolbarAttributeMenu();

			foreach (var attrib in TaskTemplate.Attributes)
			{
				var menuItem = new ToolStripMenuItem();

				menuItem.Text = attrib.Label;
				menuItem.Name = attrib.Placeholder();

				ToolStripAttributeMenu.DropDownItems.Add(menuItem);
			}

			ToolStripAttributeMenu.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Text;
			ToolStripAttributeMenu.Text = "Task Attributes";
		}

		override protected void HandleAttributeMenuClick(ToolStripMenuItem menuItem)
		{
			var selText = GetTextRange();

			if (selText != null)
				selText.text = menuItem.Name;
		}

		public void SetCustomAttributes(Dictionary<String, String> customAttributes)
		{
			foreach (var attrib in customAttributes)
			{
				var menuItem = new ToolStripMenuItem();

				menuItem.Text = String.Format("{0} (Custom)", attrib.Value);
				menuItem.Name = String.Format("$({0})", attrib.Key.ToLower());
				menuItem.Click += new System.EventHandler(base.OnAttributeMenuClick);
				
				ToolStripAttributeMenu.DropDownItems.Add(menuItem);
			}
		}
	}


} // HTMLReportExporter