﻿using System;
using System.Drawing;
using System.Windows.Forms;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

using System.IO;
using System.Reflection;
using System.Xml;
using System.Globalization;
using System.Security.Cryptography;
using System.Security.Cryptography.Xml;

namespace Abstractspoon.Tdl.PluginHelpers
{
	class RhinoLicensing
	{
		static String PUBLIC_KEY  = "<RSAKeyValue><Modulus>9twJpwt/Ofe58BOdK5Cb8XKGP5bvgxGh3IYkvCqvdzOCH3pi9BvOX+/fsRo/7HFbNmPr3Txu+hBl1JVH9ACXDxm20oKqgl6TzIk33iV6SrbuiZASi1OPAiTmsWBGKTIwrG9KiQ8JGmBotV/v2gRflqKELwiMUOO9W2DlgJ6szq0=</Modulus><Exponent>AQAB</Exponent></RSAKeyValue>";
		static String ALL_MODULES = "00000000-0000-0000-0000-000000000000";
		static char USERID_DELIM  = ':';

		public enum LicenseType
		{
			Free,
			Trial,
			Paid,
			Supporter,
			Contributor,
		};

		public static LicenseType GetLicense(String typeId)
		{
			String licType;

			if (HasLicense(ALL_MODULES, out licType))
			{
				switch (licType)
				{
					case "Supported": return LicenseType.Supporter;
					case "Contributor": return LicenseType.Contributor;
				}
			}
			else if (HasLicense(typeId, out licType))
			{
				switch (licType)
				{
					case "Free": return LicenseType.Free;
					case "Paid": return LicenseType.Paid;
				}
			}

			// all else
			return LicenseType.Trial;
		}

		private static bool HasLicense(String typeId, out String licType)
		{
			licType = String.Empty;

			String appFolder = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
			String licenseFolder = Path.Combine(appFolder, "Resources\\Licenses");

			if (Directory.Exists(licenseFolder))
			{
				var licenseFiles = Directory.EnumerateFiles(licenseFolder, typeId + ".xml", SearchOption.AllDirectories);
				var attributes = new Dictionary<String, String>();

				foreach (String licenseFile in licenseFiles)
				{
					if (!CheckLicense(PUBLIC_KEY, licenseFile, attributes))
						continue;

					// Validate attributes
					String licId, expiryDate, ownerId, pluginName, pluginId;

					if (!attributes.TryGetValue("id", out licId) ||
						!attributes.TryGetValue("expiration", out expiryDate) ||
						!attributes.TryGetValue("type", out licType) ||
						!attributes.TryGetValue("owner_id", out ownerId) ||
						!attributes.TryGetValue("plugin_name", out pluginName) ||
						!attributes.TryGetValue("plugin_id", out pluginId))
					{
						continue;
					}

					if (!pluginId.Equals(typeId))
						continue;

					String userName, domainName, computerName;

					if (!DecodeUserID(ownerId, out userName, out domainName, out computerName))
						continue;

					if (!VerifyUserIDs(userName, domainName, computerName))
						continue;

					return true;
				}
			}


			return false;
		}

		public static bool CheckLicense(String publicKey, String licensePath, /*out*/ Dictionary<String, String> attributes)
		{
			if (String.IsNullOrEmpty(publicKey) || String.IsNullOrEmpty(licensePath) || !System.IO.File.Exists(licensePath))
				return false;

			var licenseText = File.ReadAllText(licensePath);

			var doc = new XmlDocument();
			doc.LoadXml(licenseText);

			var rsa = new RSACryptoServiceProvider();
			rsa.FromXmlString(publicKey);

			var nsMgr = new XmlNamespaceManager(doc.NameTable);
			nsMgr.AddNamespace("sig", "http://www.w3.org/2000/09/xmldsig#");

			var signedXml = new SignedXml(doc);
			var sig = (XmlElement)doc.SelectSingleNode("//sig:Signature", nsMgr);

			if (sig == null)
			{
				//Log.WarnFormat("Could not find this signature node on license:\r\n{0}", License);
				return false;
			}

			signedXml.LoadXml(sig);

			if (!signedXml.CheckSignature(rsa))
				return false;

			// Extract attributes
			var license = doc.SelectSingleNode("/license");
			int att = license.Attributes.Count;

			while (att-- > 0)
			{
				var attrib = license.Attributes[att];

				attributes[attrib.Name] = attrib.Value;
			}

			return true;
		}

		private static bool TestUserIds()
		{
			var userId = EncodeUserID();

			String userName, domainName, computerName;

			if (!DecodeUserID(userId, out userName, out domainName, out computerName))
				return false;

			if (!VerifyUserIDs(userName, domainName, computerName))
				return false;

			return true;
		}

		private static void GetUserIDs(out String userName, out String domainName, out String computerName)
		{
			userName = Environment.UserName;
			domainName = Environment.UserDomainName;
			computerName = Environment.MachineName;
		}

		public static String EncodeUserID()
		{
			String userName, domainName, computerName;
			GetUserIDs(out userName, out domainName, out computerName);

			String userId = (userName + USERID_DELIM + computerName);

			if (!String.IsNullOrEmpty(domainName))
				userId = (userId + USERID_DELIM + domainName);

			var bytes = Encoding.UTF8.GetBytes(userId);
			Array.Reverse(bytes);

			return Convert.ToBase64String(bytes);
		}

		private static bool DecodeUserID(String userId, out String userName, out String domainName, out String computerName)
		{
			var bytes = Convert.FromBase64String(userId);
			Array.Reverse(bytes);

			var userIds = Encoding.UTF8.GetString(bytes).Split(USERID_DELIM);

			switch (userIds.Count())
			{
				case 2:
					userName = userIds[0];
					computerName = userIds[1];
					domainName = String.Empty;
					break;

				case 3:
					userName = userIds[0];
					computerName = userIds[1];
					domainName = userIds[2];
					break;

				default:
					userName = String.Empty;
					computerName = String.Empty;
					domainName = String.Empty;
					return false;
			}

			return true;
		}

		private static bool VerifyUserIDs(String userName, String domainName, String computerName)
		{
			String user, domain, computer;
			GetUserIDs(out user, out domain, out computer);

			return (user.Equals(userName) && domain.Equals(domainName) && computer.Equals(computerName));
		}

		// Returns the visible height of the banner
		public static int CreateBanner(String typeId, Control parent, Translator trans, int dollarPrice)
		{
			if (parent == null)
				return 0;

			// Make sure the parent doesn't already have a banner
			foreach (var ctrl in parent.Controls)
			{
				if (ctrl is RhinoLicenseBanner)
					return (ctrl as RhinoLicenseBanner).Height;
			}

			var banner = new RhinoLicenseBanner(typeId, trans, dollarPrice);

			banner.Location = new Point(0, 0);
			banner.Size = new Size(parent.ClientSize.Width, banner.Height);

			parent.Controls.Add(banner);

			return banner.Height;
		}
	}

	class RhinoLicenseBanner : Label
	{
		static String PAYPAL_URL = @"https://www.paypal.com/cgi-bin/webscr?cmd=_xclick&business=abstractspoon2%40optusnet%2ecom%2eau&item_name={0}({1})&amount={2}";

		// ---------------------------------------------

		private String m_TypeId;
		private RhinoLicensing.LicenseType m_LicenseType;
		private Translator m_Trans;
		private LinkLabel m_buyBtn;
		private int m_DollarPrice;

		// ---------------------------------------------

		public RhinoLicenseBanner(String typeId, Translator trans, int dollarPrice)
		{
			m_TypeId = typeId;
			m_Trans = trans;
			m_DollarPrice = dollarPrice;
			m_LicenseType = RhinoLicensing.GetLicense(typeId);

			InitializeComponent();
		}

		private void InitializeComponent()
		{
			Text = m_Trans.Translate(m_LicenseType.ToString() + ' ' + "License");
			Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
			TextAlign = ContentAlignment.MiddleLeft;
			//BorderStyle = BorderStyle.FixedSingle;

			Color fore, back;
			GetColors(out fore, out back);

			BackColor = back;
			ForeColor = fore;

			if (m_LicenseType == RhinoLicensing.LicenseType.Trial)
			{
				m_buyBtn = new LinkLabel();
				m_buyBtn.Text = String.Format("{0} (USD{1})", m_Trans.Translate("Buy"), m_DollarPrice);
				m_buyBtn.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Right;
				m_buyBtn.Height = Height;
				m_buyBtn.BackColor = back;
				m_buyBtn.LinkColor = fore;
				m_buyBtn.TextAlign = ContentAlignment.MiddleRight;
				m_buyBtn.LinkClicked += new LinkLabelLinkClickedEventHandler(OnBuyLicense);

				this.Controls.Add(m_buyBtn);
			}
		}

		private void OnBuyLicense(object sender, LinkLabelLinkClickedEventArgs e)
		{
			System.Diagnostics.Process.Start(FormatPaypalUrl());
		}

		public new int Height
		{
			get
			{
				switch (m_LicenseType)
				{
					case RhinoLicensing.LicenseType.Free:
						return 0;

					//case RhinoLicensing.LicenseType.Trial:
					//	return 0;

					case RhinoLicensing.LicenseType.Paid:
						return 0;

						//case RhinoLicensing.LicenseType.Supporter:
						//	return 0;

						//case RhinoLicensing.LicenseType.Contributor:
						//	return 0;
				}

				return PluginHelpers.DPIScaling.Scale(20);
			}
		}

		private void GetColors(out Color fore, out Color back)
		{
			fore = Color.Black;
			back = SystemColors.ButtonFace;

			switch (m_LicenseType)
			{
				case RhinoLicensing.LicenseType.Free:
					break;

				case RhinoLicensing.LicenseType.Trial:
					back = Color.DarkRed;
					fore = Color.White;
					break;

				case RhinoLicensing.LicenseType.Paid:
					break;

				case RhinoLicensing.LicenseType.Supporter:
					break;

				case RhinoLicensing.LicenseType.Contributor:
					break;
			}
		}

		private String FormatPaypalUrl()
		{
			return String.Format(PAYPAL_URL, m_TypeId, RhinoLicensing.EncodeUserID(), m_DollarPrice);
		}
	}
}
