#include "src\PS2E-spu2.h"
#include "AudioRenderer.h"
#include "pugixml.hpp"
#include "winconfig.h"
#include <math.h>
#include <sstream>

#define DSBVOLUME_MIN -10000
#define DSBVOLUME_MAX 0

AudioRenderer g_AudioRenderer;

extern void DSSetVolume(LONG lVolume);

SetDataCallback g_setAudioData = nullptr;

AudioRenderer::AudioRenderer(): 
	m_is_muted(0)
    , m_volume(0.5)
{
}

AudioRenderer::~AudioRenderer()
{
}

void AudioRenderer::execute(const wchar_t* a_command, wchar_t** a_result)
{
	using namespace pugi;

	xml_document l_xmlDoc;

	auto l_XMLRes = l_xmlDoc.load_string(a_command);

	if (l_XMLRes.status == xml_parse_status::status_ok)
	{
		auto l_document = l_xmlDoc.document_element();

		if (l_document.empty())
			return;

		if (std::wstring(l_document.name()) == L"Config")
		{
			auto l_ChildNode = l_document.first_child();

			while (!l_ChildNode.empty())
			{
				if (std::wstring(l_ChildNode.name()) == L"Init")
				{
					SPU2init();
					
                    auto l_Attribute = l_ChildNode.attribute(L"CaptureHandler");

                    if (!l_Attribute.empty()) {
                        auto l_value = l_Attribute.as_llong();

                        if (l_value != 0) {
                            try {

                                g_setAudioData = (SetDataCallback)l_value;

                            } catch (...) {
                            }
                        }
                    }
				}
				else if (std::wstring(l_ChildNode.name()) == L"Open")
				{
					SPU2open(nullptr);					
				}
				else if (std::wstring(l_ChildNode.name()) == L"Close")
				{
					SPU2close();
				}
				else if (std::wstring(l_ChildNode.name()) == L"Shutdown")
				{
					SPU2shutdown();

					g_setAudioData = nullptr;
				}
				else if (std::wstring(l_ChildNode.name()) == L"DoFreeze")
				{

					freezeData* l_FreezeDataHandle = nullptr;

					auto l_Attribute = l_ChildNode.attribute(L"FreezeData");

					if (!l_Attribute.empty())
					{
						auto l_value = l_Attribute.as_llong();

						if (l_value != 0)
						{
							try
							{

								l_FreezeDataHandle = (freezeData*)l_value;

							}
							catch (...)
							{

							}

						}
					}

					int l_mode = -1;

					l_Attribute = l_ChildNode.attribute(L"Mode");

					if (!l_Attribute.empty())
					{
						auto l_value = l_Attribute.as_int(-1);

						if (l_value != -1)
						{
							l_mode = l_value;
						}
					}

					if (l_FreezeDataHandle != nullptr)
					{
						SPU2freeze(l_mode, l_FreezeDataHandle);
					}
                } else if (std::wstring(l_ChildNode.name()) == L"Volume") {

                    auto l_Attribute = l_ChildNode.attribute(L"Value");

                    m_volume = l_Attribute.as_double(0.5);

					m_volume = m_volume / 100.0;

					if (m_volume > 0)
                        m_volume = exp2(exp2(m_volume)/2.0)/2.0;

					if (m_volume > 1.0)
                        m_volume = 1.0;

                    if (!l_Attribute.empty()) {

                        if (!m_is_muted)
                            DSSetVolume(((m_volume * -1) * (double)DSBVOLUME_MIN) + DSBVOLUME_MIN);
                        else
                            DSSetVolume(DSBVOLUME_MIN);
                    }
                } else if (std::wstring(l_ChildNode.name()) == L"IsMuted") {

                    auto l_Attribute = l_ChildNode.attribute(L"Value");

                    m_is_muted = l_Attribute.as_uint(0);

                    if (!l_Attribute.empty()) {

                        if (!m_is_muted)
                            DSSetVolume(((m_volume * -1) * (double)DSBVOLUME_MIN) + DSBVOLUME_MIN);
                        else
                            DSSetVolume(DSBVOLUME_MIN);
                    }
                }

				l_ChildNode = l_ChildNode.next_sibling();
			}
		} 
	}
}


