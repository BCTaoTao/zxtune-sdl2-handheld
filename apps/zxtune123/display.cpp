/**
 *
 * @file
 *
 * @brief Display component implementation
 *
 * @author vitamin.caig@gmail.com
 *
 * @par Change Log:
 * 2026-06-05 BCTaoTao: Added new data transfer methods: "--output-fd arg" and "--output-format arg"
 * 2026-06-05 BCTaoTao: Added a parameter to adjust the number of spectrum bars: "--spectrum-size arg"
 *
 **/

#include "apps/zxtune123/display.h"

#include "apps/zxtune123/console.h"

#include "module/track_state.h"
#include "module/attributes.h"
#include "parameters/template.h"
#include "platform/application.h"
#include "strings/format.h"
#include "strings/template.h"
#include "time/duration.h"
#include "time/serialize.h"

#include "error.h"
#include "string_view.h"

#include <boost/program_options.hpp>

#include <thread>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdio>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdint>
#include <vector>

namespace
{
  template<class T>
  constexpr std::size_t CountLines(const T& str)
  {
    // TODO: use std::count at C++20
    std::size_t result = 0;
    for (auto c : str)
    {
      result += c == '\n';
    }
    return result;
  }

  // clang-format off
  constexpr auto ITEM_INFO =
      "Playing: [Fullpath]\n"
      "Type:    [Type]\tContainer: [Container]\tProgram: [Program]\n"
      "Title:   [Title]\n"
      "Author:  [Author]\n"sv;
  // Exact type for Strings::Format
  constexpr std::string_view ITEM_INFO_ADDON = "\nTime:    {0}\tLoop duration:  {1}\n\n";
  constexpr std::string_view TRACKING_FORMAT =
        "Position: {0:<6}Line:     {2:<6}Channels: {4:<6}\n"
        "Pattern:  {1:<6}Frame:    {3:<6}Tempo:    {5:<6}\n"
        "\n";
  constexpr std::string_view PLAYBACK_STATUS = "[{0}] [{1}]\n";
  // clang-format on

  // layout constants
  constexpr auto INFORMATION_HEIGHT = CountLines(ITEM_INFO) + CountLines(ITEM_INFO_ADDON);
  constexpr auto TRACKING_HEIGHT = CountLines(TRACKING_FORMAT);
  constexpr auto PLAYING_HEIGHT = CountLines(PLAYBACK_STATUS);

  constexpr uint8_t FRAME_MAGIC = 0x5A;
  constexpr uint8_t FRAME_TYPE_INFO = 0x01;
  constexpr uint8_t FRAME_TYPE_STATE = 0x02;
  constexpr uint8_t PLAYBACK_STARTED = 0;
  constexpr uint8_t PLAYBACK_PAUSED = 1;
  constexpr uint8_t PLAYBACK_STOPPED = 2;
  constexpr uint8_t FLAG_HAS_TRACK_STATE = 0x01;
  constexpr uint8_t FLAG_HAS_SPECTRUM = 0x02;

  void BinU16LE(std::vector<uint8_t>& buf, uint16_t val)
  {
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
  }

  void BinU32LE(std::vector<uint8_t>& buf, uint32_t val)
  {
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
  }

  void BinString(std::vector<uint8_t>& buf, const String& str)
  {
    const uint16_t len = static_cast<uint16_t>(std::min<std::size_t>(str.size(), 65535));
    BinU16LE(buf, len);
    buf.insert(buf.end(), str.data(), str.data() + len);
  }

  void BinHeader(std::vector<uint8_t>& buf, uint8_t type)
  {
    const uint16_t payloadLen = static_cast<uint16_t>(buf.size() - 4);
    buf[0] = FRAME_MAGIC;
    buf[1] = type;
    buf[2] = static_cast<uint8_t>(payloadLen & 0xFF);
    buf[3] = static_cast<uint8_t>((payloadLen >> 8) & 0xFF);
  }

  String EscapeJsonString(StringView str)
  {
    std::ostringstream oss;
    oss << '"';
    for (char c : str)
    {
      switch (c)
      {
      case '"':
        oss << "\\\"";
        break;
      case '\\':
        oss << "\\\\";
        break;
      case '\b':
        oss << "\\b";
        break;
      case '\f':
        oss << "\\f";
        break;
      case '\n':
        oss << "\\n";
        break;
      case '\r':
        oss << "\\r";
        break;
      case '\t':
        oss << "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20)
        {
          oss << "\\u" << std::hex << std::setfill('0') << std::setw(4) << static_cast<int>(c);
        }
        else
        {
          oss << c;
        }
      }
    }
    oss << '"';
    return oss.str();
  }

  class DisplayComponentImpl : public DisplayComponent
  {
  public:
    DisplayComponentImpl()
      : Options("Display-related options")
      , InformationTemplate(Strings::Template::Create(ITEM_INFO))
      , ScrSize(Console::Self().GetSize())
      , OutputFd(-1)
      , FdValid(false)
      , BinaryFormat(false)
    {
      using namespace boost::program_options;
      auto opt = Options.add_options();
      opt("silent", bool_switch(&Silent), "disable all output");
      opt("quiet", bool_switch(&Quiet), "disable dynamic output");
      opt("analyzer", bool_switch(&ShowAnalyze), "enable spectrum analyzer");
      opt("updatefps", value<uint_t>(&Updatefps), "update rate");

      opt("output-fd", value<int_t>(&OutputFd)->default_value(-1), "file descriptor to write output (default: -1 = console)");
      opt("output-format", value<String>(&OutputFormatStr)->default_value("json"), "output format: json or binary");
      opt("spectrum-size", value<uint_t>(&SpectrumSize)->default_value(64), "number of spectrum bands for output-fd mode");
    }

    const boost::program_options::options_description& GetOptionsDescription() const override
    {
      return Options;
    }

    void Message(StringView msg) override
    {
      if (!Silent)
      {
        if (OutputFd == -1)
        {
          Console::Self().Write(msg);
          StdOut << std::endl;
        }
      }
    }

    void SetModule(Module::Holder::Ptr module, Sound::Backend::Ptr player) override
    {
      const auto info = module->GetModuleInformation();
      const auto props = module->GetModuleProperties();
      TotalDuration = info->Duration();
      State = player->GetState();
      TrackState = dynamic_cast<const Module::TrackState*>(State.get());
      if (!Silent && ShowAnalyze)
      {
        Analyzer = player->GetAnalyzer();
      }
      else
      {
        Analyzer.reset();
      }

      if (OutputFd != -1)
      {
        BinaryFormat = (OutputFormatStr == "binary");
        FdValid = true;

        String fullpath = Parameters::GetString(*props, Module::ATTR_FULLPATH);
        String type = Parameters::GetString(*props, Module::ATTR_TYPE);
        String container = Parameters::GetString(*props, Module::ATTR_CONTAINER);
        String program = Parameters::GetString(*props, Module::ATTR_PROGRAM);
        String title = Parameters::GetString(*props, Module::ATTR_TITLE);
        String author = Parameters::GetString(*props, Module::ATTR_AUTHOR);

        if (BinaryFormat)
        {
          WriteInfoBinary(fullpath, type, container, program, title, author, info->LoopDuration());
        }
        else
        {
          WriteInfoJson(fullpath, type, container, program, title, author, info->LoopDuration());
        }
      }
      else if (!Silent)
      {
        Console::Self().Write(
            InformationTemplate->Instantiate(Parameters::FieldsSourceAdapter<Strings::FillFieldsSource>(*props)));
        StdOut << Strings::Format(ITEM_INFO_ADDON, Time::ToString(TotalDuration), Time::ToString(info->LoopDuration()));
      }
      DynamicLines = 0;
    }

    Time::AtMillisecond BeginFrame(Sound::PlaybackControl::State state) override
    {
      const auto curPos = State->At();
      if (Silent || Quiet)
      {
        return curPos;
      }

      if (OutputFd != -1 && FdValid)
      {
        Vsync();
        if (BinaryFormat)
        {
          WriteStateBinary(curPos, state);
        }
        else
        {
          WriteStateJson(curPos, state);
        }
        return curPos;
      }

      ScrSize = Console::Self().GetSize();
      if (ScrSize.first <= 0 || ScrSize.second <= 0)
      {
        Silent = true;
        return curPos;
      }
      const int_t trackingHeight = TrackState ? TRACKING_HEIGHT : 0;
      const int_t spectrumHeight = ScrSize.second - INFORMATION_HEIGHT - trackingHeight - PLAYING_HEIGHT - 1;
      if (spectrumHeight < 4)
      {
        Analyzer.reset();
      }
      else if (ScrSize.second < int_t(trackingHeight + PLAYING_HEIGHT))
      {
        Quiet = true;
      }
      else
      {
        Vsync();
        if (TrackState)
        {
          ShowTrackingStatus(*TrackState);
        }
        ShowPlaybackStatus(Time::Milliseconds(curPos.CastTo<Time::Millisecond>().Get()), state);
        if (Analyzer)
        {
          Sound::Analyzer::LevelType spectrum[ScrSize.first];
          Analyzer->GetSpectrum(spectrum, ScrSize.first);
          AnalyzerData.resize(ScrSize.first);
          UpdateAnalyzer(spectrum);
          ShowAnalyzer(spectrumHeight);
        }
        StdOut << std::flush;
      }
      return curPos;
    }

  private:
    void WriteInfoJson(const String& fullpath, const String& type, const String& container,
                       const String& program, const String& title, const String& author,
                       Time::Milliseconds loopDuration)
    {
      if (!FdValid || OutputFd < 0) return;

      std::ostringstream os;
      os << "{\"type\":\"info\","
         << "\"fullpath\":" << EscapeJsonString(fullpath) << ","
         << "\"type\":" << EscapeJsonString(type) << ","
         << "\"container\":" << EscapeJsonString(container) << ","
         << "\"program\":" << EscapeJsonString(program) << ","
         << "\"title\":" << EscapeJsonString(title) << ","
         << "\"author\":" << EscapeJsonString(author) << ","
         << "\"total_duration_ms\":" << TotalDuration.Get() << ","
         << "\"loop_duration_ms\":" << loopDuration.Get()
         << "}\n";

      String outStr = os.str();
      if (write(OutputFd, outStr.data(), outStr.size()) != static_cast<ssize_t>(outStr.size()))
      {
        FdValid = false;
      }
    }

    void WriteInfoBinary(const String& fullpath, const String& type, const String& container,
                         const String& program, const String& title, const String& author,
                         Time::Milliseconds loopDuration)
    {
      try
      {
        BinBuf.clear();
        BinBuf.resize(4);
        BinU32LE(BinBuf, static_cast<uint32_t>(TotalDuration.Get()));
        BinU32LE(BinBuf, static_cast<uint32_t>(loopDuration.Get()));
        BinString(BinBuf, fullpath);
        BinString(BinBuf, type);
        BinString(BinBuf, container);
        BinString(BinBuf, program);
        BinString(BinBuf, title);
        BinString(BinBuf, author);
        BinHeader(BinBuf, FRAME_TYPE_INFO);
        WriteBinBuf();
      }
      catch (...)
      {
        FdValid = false;
      }
    }

    void WriteStateJson(Time::AtMillisecond curPos, Sound::PlaybackControl::State state)
    {
      if (!FdValid || OutputFd < 0) return;

      std::ostringstream os;
      String stateStr;
      switch (state)
      {
      case Sound::PlaybackControl::STARTED:
        stateStr = "started";
        break;
      case Sound::PlaybackControl::PAUSED:
        stateStr = "paused";
        break;
      default:
        stateStr = "stopped";
        break;
      }

      os << "{\"type\":\"state\","
         << "\"position_ms\":" << curPos.CastTo<Time::Millisecond>().Get() << ","
         << "\"state\":" << EscapeJsonString(stateStr);

      if (TrackState)
      {
        os << ",\"position\":" << TrackState->Position()
           << ",\"pattern\":" << TrackState->Pattern()
           << ",\"line\":" << TrackState->Line()
           << ",\"quirk\":" << TrackState->Quirk()
           << ",\"channels\":" << TrackState->Channels()
           << ",\"tempo\":" << TrackState->Tempo();
      }

      if (Analyzer)
      {
        std::vector<Sound::Analyzer::LevelType> spectrum(SpectrumSize);
        Analyzer->GetSpectrum(spectrum.data(), SpectrumSize);
        if (AnalyzerData.size() != SpectrumSize)
        {
          AnalyzerData.resize(SpectrumSize, 0);
        }
        UpdateAnalyzer(spectrum.data());

        os << ",\"spectrum\":[";
        for (std::size_t i = 0; i < SpectrumSize; ++i)
        {
          if (i > 0) os << ",";
          int val = (AnalyzerData[i] * 255) / Sound::Analyzer::LevelType::PRECISION;
          os << std::max(0, std::min(255, val));
        }
        os << "]";
      }

      os << "}\n";

      String outStr = os.str();
      if (write(OutputFd, outStr.data(), outStr.size()) != static_cast<ssize_t>(outStr.size()))
      {
        FdValid = false;
      }
    }

    void WriteStateBinary(Time::AtMillisecond curPos, Sound::PlaybackControl::State state)
    {
      try
      {
        BinBuf.clear();
        BinBuf.resize(4);

        BinU32LE(BinBuf, static_cast<uint32_t>(curPos.CastTo<Time::Millisecond>().Get()));

        uint8_t stateByte = PLAYBACK_STOPPED;
        switch (state)
        {
        case Sound::PlaybackControl::STARTED:
          stateByte = PLAYBACK_STARTED;
          break;
        case Sound::PlaybackControl::PAUSED:
          stateByte = PLAYBACK_PAUSED;
          break;
        default:
          break;
        }
        BinBuf.push_back(stateByte);

        uint8_t flags = 0;
        if (TrackState)
        {
          flags |= FLAG_HAS_TRACK_STATE;
        }
        if (Analyzer)
        {
          flags |= FLAG_HAS_SPECTRUM;
        }
        BinBuf.push_back(flags);

        if (TrackState)
        {
          BinU32LE(BinBuf, static_cast<uint32_t>(TrackState->Position()));
          BinU32LE(BinBuf, static_cast<uint32_t>(TrackState->Pattern()));
          BinU32LE(BinBuf, static_cast<uint32_t>(TrackState->Line()));
          BinU32LE(BinBuf, static_cast<uint32_t>(TrackState->Quirk()));
          BinU32LE(BinBuf, static_cast<uint32_t>(TrackState->Channels()));
          BinU32LE(BinBuf, static_cast<uint32_t>(TrackState->Tempo()));
        }

        if (Analyzer)
        {
          std::vector<Sound::Analyzer::LevelType> spectrum(SpectrumSize);
          Analyzer->GetSpectrum(spectrum.data(), SpectrumSize);
          if (AnalyzerData.size() != SpectrumSize)
          {
            AnalyzerData.resize(SpectrumSize, 0);
          }
          UpdateAnalyzer(spectrum.data());

          BinBuf.push_back(static_cast<uint8_t>(SpectrumSize)); // 动态长度
          for (std::size_t i = 0; i < SpectrumSize; ++i)
          {
            int val = (AnalyzerData[i] * 255) / Sound::Analyzer::LevelType::PRECISION;
            BinBuf.push_back(static_cast<uint8_t>(std::max(0, std::min(255, val))));
          }
        }

        BinHeader(BinBuf, FRAME_TYPE_STATE);
        WriteBinBuf();
      }
      catch (...)
      {
        FdValid = false;
      }
    }

    void WriteBinBuf()
    {
      if (!FdValid || OutputFd < 0)
      {
        return;
      }
      const ssize_t written = write(OutputFd, BinBuf.data(), BinBuf.size());
      if (written != static_cast<ssize_t>(BinBuf.size()))
      {
        FdValid = false;
      }
    }

    void Vsync()
    {
      if (NextFrameStart != decltype(NextFrameStart){})
      {
        std::this_thread::sleep_until(NextFrameStart);
      }
      else
      {
        NextFrameStart = std::chrono::steady_clock::now();
      }
      const uint_t waitPeriod(std::max<uint_t>(1, 1000 / std::max<uint_t>(Updatefps, 1)));
      NextFrameStart += std::chrono::milliseconds(waitPeriod);
      if (OutputFd == -1)
      {
        if (DynamicLines)
        {
          Console::Self().MoveCursorUp(DynamicLines);
        }
        DynamicLines = 0;
      }
    }

    void ShowTrackingStatus(const Module::TrackState& state)
    {
      StdOut << Strings::Format(TRACKING_FORMAT, state.Position(), state.Pattern(), state.Line(), state.Quirk(),
                                state.Channels(), state.Tempo());
      DynamicLines += TRACKING_HEIGHT;
    }

    void ShowPlaybackStatus(Time::Milliseconds played, Sound::PlaybackControl::State state)
    {
      const auto MARKER = '\x1';
      String data = Strings::Format(PLAYBACK_STATUS, Time::ToString(played), MARKER);
      const String::size_type totalSize = data.size() - 1 - PLAYING_HEIGHT;
      const String::size_type markerPos = data.find(MARKER);

      String prog(ScrSize.first - totalSize, '-');
      const auto pos = (played * (ScrSize.first - totalSize)).Divide<uint_t>(TotalDuration);
      prog[pos] = StateSymbol(state);
      data.replace(markerPos, 1, prog);
      StdOut << data;
      DynamicLines += PLAYING_HEIGHT;
    }

    static char StateSymbol(Sound::PlaybackControl::State state)
    {
      switch (state)
      {
      case Sound::PlaybackControl::STARTED:
        return '>';
      case Sound::PlaybackControl::PAUSED:
        return '#';
      default:
        return '\?';
      }
    }

    void ShowAnalyzer(uint_t high)
    {
      const std::size_t width = AnalyzerData.size();
      std::string buffer(width, ' ');
      for (int_t y = high; y; --y)
      {
        const int_t limit = (y - 1) * Sound::Analyzer::LevelType::PRECISION / high;
        std::transform(AnalyzerData.begin(), AnalyzerData.end(), buffer.begin(),
                       [limit](const int_t val) { return val > limit ? '#' : ' '; });
        StdOut << buffer << '\n';
      }
      DynamicLines += high;
    }

    void UpdateAnalyzer(const Sound::Analyzer::LevelType* inState)
    {
      const auto falling = Sound::Analyzer::LevelType::PRECISION / Updatefps;
      for (uint_t band = 0, lim = AnalyzerData.size(); band < lim; ++band)
      {
        AnalyzerData[band] = std::max<int>(AnalyzerData[band] - falling, int_t(inState[band].Raw()));
      }
    }

  private:
    boost::program_options::options_description Options;
    bool Silent = false;
    bool Quiet = false;
    bool ShowAnalyze = false;
    uint_t Updatefps = 10;
    uint_t SpectrumSize = 64;
    const Strings::Template::Ptr InformationTemplate;
    std::chrono::time_point<std::chrono::steady_clock> NextFrameStart;
    std::size_t DynamicLines = 0;
    Console::SizeType ScrSize;
    Time::Milliseconds TotalDuration;
    Module::State::Ptr State;
    const Module::TrackState* TrackState;
    Sound::Analyzer::Ptr Analyzer;
    std::vector<int_t> AnalyzerData;
    int_t OutputFd;
    bool FdValid;
    String OutputFormatStr;
    bool BinaryFormat;
    std::vector<uint8_t> BinBuf;
  };
}  // namespace

DisplayComponent::Ptr DisplayComponent::Create()
{
  return DisplayComponent::Ptr(new DisplayComponentImpl);
}
