#include "UltraEngine.h"

using namespace UltraEngine;

// https://www.ultraengine.com/community/blogs/entry/2780-building-a-single-file-4k-hdr-skybox-with-bc6-compression/
int main(int argc, const char* argv[])
{
    EngineSettings settings;
    settings.asyncrender = false;
    Initialize(settings);

    //Settings
    const bool compression = true;

    // Load required plugin
    auto fiplugin = LoadPlugin("Plugins/FITextureLoader");
    auto cplugin = LoadPlugin("Plugins/ISPCTexComp");

    //if (FileType("skybox.dds", false) == 0)
    {
        // Load cube faces output from https://matheowis.github.io/HDRI-to-CubeMap/
        std::vector<std::shared_ptr<Pixmap> > mipchain;
        WString files[6] = { "px.hdr", "nx.hdr", "py.hdr", "ny.hdr", "pz.hdr", "nz.hdr" };
        for (int n = 0; n < 6; ++n)
        {
            auto pixmap = LoadPixmap(files[n]);
            Assert(pixmap);
            if (pixmap->format != VK_FORMAT_R16G16B16A16_SFLOAT)
            {
                pixmap = pixmap->Convert(TextureFormat(VK_FORMAT_R16G16B16A16_SFLOAT));// this step is required for BC6H conversion
            }
            while (true)
            {
                auto mipmap = pixmap;

                if (compression) mipmap = mipmap->Convert(TEXTURE_BC6H);
                Assert(mipmap);
                mipchain.push_back(mipmap);
                
                auto size = pixmap->size;
                if (size.x == mipmap->blocksize and size.y == mipmap->blocksize) break;

                size /= 2;
                pixmap = pixmap->Resize(size.x, size.y);
                Assert(pixmap);
            }
        }

        // Save cubemap
        SaveTexture("skybox.dds", TEXTURE_CUBE, mipchain, 6);
    }

    auto displays = GetDisplays();
    auto win = CreateWindow("Render Reflection Map", 0, 0, 1024, 1024, displays[0], WINDOW_TITLEBAR | WINDOW_CLIENTCOORDS);
    auto framebuffer = CreateFramebuffer(win);
    auto world = CreateWorld();
    auto cam = CreateCamera(world);
    cam->SetDepthPrepass(false);
    cam->SetLighting(false);

    auto fx = LoadPostEffect("DiffuseIBLFilter.json");
    cam->AddPostEffect(fx);

    world->Render(framebuffer);
    world->Render(framebuffer);
    world->Render(framebuffer);

    auto tex = LoadTexture("skybox.dds");
    
    //Generate specular reflection map
    {
        std::vector<std::shared_ptr<Pixmap> > mipchain;
        std::map<int, shared_ptr<TextureBuffer> > texbuffers;

        float roughness = 0.0f;
        int distribution = 1;// 0: diffuse, 1: specular

        int size = 2048;
        int mipcount = 10;

        std::array<std::vector<shared_ptr<Pixmap> >, 6> chains;

        int w = size;
        int miplevel = 0;

        while (true)
        {
            for (int n = 0; n < 6; ++n)
            {
                roughness = float(miplevel) / float(mipcount - 1);

                if (not texbuffers[w]) texbuffers[w] = CreateTextureBuffer(w, w, 1, false);
                cam->SetRenderTarget(texbuffers[w]);

                cam->SetPostEffectParameter(0, 0, roughness);
                cam->SetPostEffectParameter(0, 1, miplevel);
                cam->SetPostEffectParameter(0, 2, w);
                cam->SetPostEffectParameter(0, 3, distribution);
                cam->SetPostEffectParameter(0, 4, tex);
                cam->SetPostEffectParameter(0, 5, n);

                texbuffers[w]->Capture();
                world->Render(framebuffer);
                auto caps = texbuffers[w]->GetCaptures();
                Assert(not caps.empty());

                if (compression) caps[0] = caps[0]->Convert(TEXTURE_BC6H);

                //tex->SetPixels(caps[0], miplevel, n);

                //mipchain.push_back(caps[0]);
                chains[n].push_back(caps[0]);
            }
            if (w == 4) break;
            w /= 2;
            ++miplevel;
        }

        for (int n = 0; n < 6; ++n)
        {
            mipchain.insert(mipchain.end(), chains[n].begin(), chains[n].end());
        }

        // Save diffuse reflection map
        SaveTexture("specular.dds", TEXTURE_CUBE, mipchain, 6);
    }

    //Generate diffuse reflection map
    {
        auto texbuffer = CreateTextureBuffer(512, 512, 1, false);
        cam->SetRenderTarget(texbuffer);

        std::vector<std::shared_ptr<Pixmap> > mipchain;

        float roughness = 0.0f;
        int miplevel = 0;
        int width = texbuffer->GetSize().x;
        int distribution = 0;// 0: diffuse, 1: specular

        for (int n = 0; n < 6; ++n)
        {
            cam->SetPostEffectParameter(0, 0, roughness);
            cam->SetPostEffectParameter(0, 1, miplevel);
            cam->SetPostEffectParameter(0, 2, width);
            cam->SetPostEffectParameter(0, 3, distribution);
            cam->SetPostEffectParameter(0, 4, tex);
            cam->SetPostEffectParameter(0, 5, n);

            texbuffer->Capture();
            world->Render(framebuffer);
            auto caps = texbuffer->GetCaptures();
            Assert(not caps.empty());
            if (compression) caps[0] = caps[0]->Convert(TEXTURE_BC6H);
            mipchain.push_back(caps[0]);
        }

        // Save diffuse reflection map
        SaveTexture("diffuse.dds", TEXTURE_CUBE, mipchain, 6);
    }

    return 0;
}