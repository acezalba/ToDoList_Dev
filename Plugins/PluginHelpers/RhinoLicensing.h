#pragma once

////////////////////////////////////////////////////////////////////////////////////////////////

using namespace System;
using namespace System::Collections::Generic;

////////////////////////////////////////////////////////////////////////////////////////////////

namespace Abstractspoon
{
	namespace Tdl
	{
		namespace PluginHelpers
		{
			public ref class RhinoLicensing
			{
			public:
				enum class LicenseType
				{
					Trial,
					Paid,
					Supporter,
				};

			public:
				static LicenseType GetLicense(String^ typeId);
				static bool CheckLicense(String^ publicKey, String^ licensePath, /*out*/ Dictionary<String^, String^>^ attributes);

			protected:
				static bool CheckLicense(String^ typeId);

			};
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////

