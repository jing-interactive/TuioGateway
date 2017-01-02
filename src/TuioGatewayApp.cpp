#include "cinder/Cinder.h"
#include "cinder/app/App.h"
#include "cinder/gl/gl.h"
#include "cinder/app/RendererGl.h"
#include "cinder/System.h"
#include "cinder/Utilities.h"
#include "cinder/tuio/Tuio.h"
#include "cinder/osc/Osc.h"
#include "cinder/params/Params.h"

#include "Cinder-VNM/include/MiniConfig.h"

#include <vector>
#include <unordered_map>

using namespace ci;
using namespace ci::app;
using namespace std;

#define OSC_MODE 0

struct MyCursor
{
    MyCursor()
    {
    }

    MyCursor(const tuio::Cursor2d& cursor2d) :
        mSessionId(cursor2d.getSessionId()),
        mSource(cursor2d.getSource()),
        mPosition(cursor2d.getPosition()),
        mVelocity(cursor2d.getVelocity()),
        mAcceleration(cursor2d.getAcceleration())
    {

    }
    int32_t		mSessionId;
    string      mSource;
    vec2		mPosition, mVelocity;
    float		mAcceleration;
};

class TuioGateway : public App {
public:

    unordered_map<uint32_t, MyCursor> mActiveCursors;

    void mouseDown(MouseEvent event)
    {
        mouseDrag(event);
    }

    void mouseUp(MouseEvent event)
    {
        mCursorPressed = false;
    }

    void mouseDrag(MouseEvent event)
    {
        mCursorPressed = true;
        mCursorPos = event.getPos();
    }

    enum { USAGE_CLIENT, USAGE_SERVER, USAGE_ROUTER, USAGE_COUNT };

    void onConnect()
    {
        APP_USAGE = math<int>::clamp(APP_USAGE, USAGE_CLIENT, USAGE_ROUTER);

        mCursorPressed = false;

#if OSC_MODE
        mOscServer = osc::SenderUdp();
#endif

        const char* kUsageDescs[] =
        {
            "TuioGateway - Client Mode",
            "TuioGateway - Server Mode",
            "TuioGateway - Router Mode",
        };
        getWindow()->setTitle(kUsageDescs[APP_USAGE]);

        char buffer[MAX_PATH] = { 0 };
        strcpy(buffer, mEnumTypes[APP_USAGE].c_str());

        if (APP_USAGE != USAGE_SERVER)
        {
            mOscReceiver = std::make_shared<osc::ReceiverUdp>(LOCAL_TUIO_PORT);
            mTuio = std::make_shared<tuio::Receiver>(mOscReceiver.get());

            mTuio->setAddedFn<tuio::Cursor2d>([&](const tuio::Cursor2d& cursor) {
                mActiveCursors.insert(make_pair(cursor.getSessionId(), cursor));
            });
            mTuio->setUpdatedFn<tuio::Cursor2d>([&](const tuio::Cursor2d& cursor) {
                mActiveCursors[cursor.getSessionId()] = cursor;
            });
            mTuio->setRemovedFn<tuio::Cursor2d>([&](const tuio::Cursor2d& cursor) {
                mActiveCursors.erase(cursor.getSessionId());
            });

            try
            {
                mOscReceiver->bind();
                sprintf_s(buffer, MAX_PATH, "%s | listen at #%d", buffer, LOCAL_TUIO_PORT);
                mCurrentAppUsage = APP_USAGE;
            }
            catch (const ci::Exception &ex)
            {
                CI_LOG_EXCEPTION("OscReceiver bind", ex);
                sprintf_s(buffer, MAX_PATH, "%s | [FAIL]!!!", buffer);
            }

            // And listen for messages.
            mOscReceiver->listen([](asio::error_code ec, asio::ip::udp::endpoint ep) -> bool {
                if (ec) {
                    CI_LOG_E("Error on listener: " << ec.message() << " Error Value: " << ec.value());
                    return false;
                }
                else
                    return true;
            });
        }

        if (APP_USAGE != USAGE_CLIENT)
        {
#if OSC_MODE
            mOscServer.setup(REMOTE_IP, REMOTE_OSC_PORT);
            sprintf_s(buffer, MAX_PATH, "%s | sending to %s: #%d | #%d",
                buffer, REMOTE_IP.c_str(), REMOTE_TUIO_PORT, REMOTE_OSC_PORT);
#else
            mOscSender = std::make_shared<osc::SenderUdp>(10000, REMOTE_IP, REMOTE_TUIO_PORT);
            try
            {
                mOscSender->bind();
                sprintf_s(buffer, MAX_PATH, "%s | sending to %s: #%d", buffer, REMOTE_IP.c_str(), REMOTE_TUIO_PORT);
                mCurrentAppUsage = APP_USAGE;
            }
            catch (const ci::Exception &ex)
            {
                CI_LOG_EXCEPTION("mOscSender bind", ex);
                sprintf_s(buffer, MAX_PATH, "%s | [FAIL]!!!", buffer);
            }
#endif
        }

        mStatus = buffer;
    }

    void setup()
    {
        console() << "TuioGateway built on " << __DATE__ << endl;

        readConfig();
        mStatus = "idle..press CONNECT button";
        mCurrentAppUsage = USAGE_COUNT;

        mParams = params::InterfaceGl("param", { 270, 240 });
        {
            mEnumTypes.clear();
            mEnumTypes.push_back("Client");
            mEnumTypes.push_back("Server");
            mEnumTypes.push_back("Router");

            // MAGIC!
            int lines = 0;
#define ITEM_DEF(type, var, default) mParams.addParam(#var, &var);
#include "item.def"
#undef ITEM_DEF

            // HACK!
            mParams.removeParam("APP_USAGE");
            mParams.addSeparator();
            mParams.addParam("APP_USAGE", mEnumTypes, &APP_USAGE);
            mParams.addSeparator();
            mParams.addButton("CONNECT", bind(&TuioGateway::onConnect, this));
            mParams.addButton("SAVE", writeConfig);

            onConnect();
        }

        console() << "MT: " << System::hasMultiTouch() << " Max points: " << System::getMaxMultiTouchPoints() << std::endl;

        mFont = Font("YouYuan", 22);
    }

    void update()
    {
        N_DISPLAYS = math<int>::clamp(N_DISPLAYS, 1, 8);
        REMOTE_DISPLAY_ID = math<int>::clamp(REMOTE_DISPLAY_ID, 1, N_DISPLAYS);

        if (mCursorPressed)
        {
            MyCursor cursor = {};
            cursor.mSessionId = -1;
            cursor.mPosition = { mCursorPos.x / getWindowWidth(), mCursorPos.y / getWindowHeight() };
            mActiveCursors[cursor.mSessionId] = cursor;
        }
        else
        {
            mActiveCursors.erase(-1);
        }

        if (mCurrentAppUsage == USAGE_SERVER || mCurrentAppUsage == USAGE_ROUTER)
        {
            sendTuioMessage(*mOscSender, mActiveCursors);
#if OSC_MODE
            endOscMessages(*mOscServer, mActiveCursors);
#endif
        }
    }

    void draw()
    {
        gl::enableAlphaBlending();
        gl::clear(Color(0.1f, 0.1f, 0.1f));
        gl::setMatricesWindow(getWindowWidth(), getWindowHeight());

        if (VISUAL_EFFECT)
        {
            // draw yellow circles at the active touch points
            gl::color(Color(1, 1, 0));

            for (auto& it : mActiveCursors)
                gl::drawStrokedCircle(vec2(it.second.mPosition.x * getWindowWidth(), it.second.mPosition.y * getWindowHeight()), 20.0f);

            gl::drawString(mStatus, vec2(10, getWindowHeight() - 100), ColorA::white(), mFont);
        }

        mParams.draw();
    }

    void keyDown(KeyEvent event) {
        switch (event.getCode())
        {
        case KeyEvent::KEY_ESCAPE:
        {
            quit();
        }break;
        case KeyEvent::KEY_h:
        {
            if (mParams.isVisible())
            {
                mParams.show(false);
                VISUAL_EFFECT = false;
            }
            else
            {
                mParams.show(true);
                VISUAL_EFFECT = true;
            }
        }
        default:
            break;
        }
    }

    void sendTuioMessage(osc::SenderUdp& sender, const unordered_map<uint32_t, MyCursor>& cursors)
    {
        osc::Bundle b;
        osc::Message alive;
        // Sends alive message - saying 'Hey, there's no alive blobs'
        alive.setAddress("/tuio/2Dcur");
        alive.append("alive");

        // Send fseq message
        osc::Message fseq;
        fseq.setAddress("/tuio/2Dcur");
        fseq.append("fseq");
        fseq.append((int32_t)getElapsedFrames());

        if (!cursors.empty())
        {
            float cellSize = 1.0f / N_DISPLAYS;
            float x0 = cellSize * (REMOTE_DISPLAY_ID - 1);
            float x1 = cellSize * (REMOTE_DISPLAY_ID);

            for (auto& kv : cursors)
            {
                auto& cursor = kv.second;
                float x = cursor.mPosition.x;
                float y = cursor.mPosition.y;

                x = x0 + x * cellSize;
                //y = y0 + y * cellSize;

                if (x <= x0 || x >= x1 || y <= 0 || y >= 1.0f)
                    continue;

                osc::Message set;
                set.setAddress("/tuio/2Dcur");
                set.append("set");
                set.append(cursor.mSessionId);      // id
                set.append(x);	                        // x
                set.append(y);	                        // y
                set.append(cursor.mVelocity.x);     // dX
                set.append(cursor.mVelocity.y);     // dY
                set.append(cursor.mAcceleration);   // m

                b.append(set);							// add message to bundle
                alive.append(cursor.mSessionId);    // add blob to list of ALL active IDs
            }
        }
        b.append(alive);		// add message to bundle
        b.append(fseq);		// add message to bundle

        try
        {
            sender.send(b); // send bundle
        }
        catch (std::exception& e)
        {
            console() << "send : " << e.what() << endl;
        }
    }

    void sendOscMessages(osc::SenderUdp& sender, const unordered_map<uint32_t, MyCursor>& cursors)
    {
        if (!cursors.empty())
        {
            float cellSize = 1.0f / N_DISPLAYS;
            float x0 = cellSize * (REMOTE_DISPLAY_ID - 1);
            float x1 = cellSize * (REMOTE_DISPLAY_ID);

            int cursorIdx = 0;
            for (auto& kv : cursors)
            {
                if (cursorIdx == MAX_CURSOR_COUNT)
                    break;

                auto& cursor = kv.second;

                float x = cursor.mPosition.x;
                float y = cursor.mPosition.y;

                x = x0 + x * cellSize;
                //y = y0 + y * cellSize;

                if (x <= x0 || x >= x1 || y <= 0 || y >= 1.0f)
                    continue;

                std::string addr = "/cursor/" + toString(cursorIdx);
                osc::Bundle bundle;
                {
                    osc::Message m;
                    m.setAddress(addr + "/x");
                    m.append(x);
                    bundle.append(m);
                }
                {
                    osc::Message m;
                    m.setAddress(addr + "/y");
                    m.append(y);
                    bundle.append(m);
                }
                sender.send(bundle);

                cursorIdx++;
            }
        }
    }

private:
    params::InterfaceGl mParams;

    std::shared_ptr<tuio::Receiver>     mTuio;
    std::shared_ptr<osc::SenderUdp>     mOscSender;
    std::shared_ptr<osc::ReceiverUdp>   mOscReceiver;
#if OSC_MODE
    osc::SenderUdp                 mOscServer;
#endif

    vector<string>              mEnumTypes;
    string                      mStatus;
    Font                        mFont;

    mutex                       mMutex;
    bool                        mCursorPressed;
    vec2                       mCursorPos;

    int                         mCurrentAppUsage;
};

void preSettings(App::Settings *settings)
{
    //settings->setWindowSize(1200, 800);
#if defined( CINDER_MSW_DESKTOP )
    settings->setConsoleWindowEnabled();
#endif
    settings->setMultiTouchEnabled();
}

CINDER_APP(TuioGateway, RendererGl, preSettings)

