// ARCA no xOS - Vistas/InstrumentoShader (de Vistas\InstrumentoShader.cs e, no papel do
// d3dcompiler, o compilador GLSL do driver).
#include "InstrumentoShader.h"

#include "Janela.h"

#include <GLES2/gl2.h>
#include "nanovg/nanovg.h"
#include "nanovg/nanovg_gl.h"

#include <cmath>

namespace ARCA::Vistas {

namespace {

// O quadrado que cobre o alvo inteiro; uv com (0,0) em cima à esquerda (o TEXCOORD do WPF).
const char *const VertexShader =
    "attribute vec2 pos;\n"
    "varying vec2 uv;\n"
    "void main() {\n"
    "    uv = vec2((pos.x + 1.0) * 0.5, (1.0 - pos.y) * 0.5);\n"
    "    gl_Position = vec4(pos, 0.0, 1.0);\n"
    "}\n";

// O que o HLSL declarava: sampler2D dados : register(s1); float quadro : register(c0). A entrada
// (s0, a imagem do próprio Border) não era usada por nenhum dos três.
const char *const Cabecalho =
    "precision mediump float;\n"
    "varying vec2 uv;\n"
    "uniform sampler2D dados;\n"
    "uniform float quadro;\n";

unsigned Compila(unsigned tipo, const std::string &fonte, std::string &erro)
{
    GLuint s = glCreateShader(tipo);
    const char *p = fonte.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024] = {};
        glGetShaderInfoLog(s, sizeof log - 1, nullptr, log);
        erro = log[0] ? log : "sem detalhe";
        glDeleteShader(s);
        return 0;
    }
    return s;
}

} // namespace

InstrumentoShader::InstrumentoShader()
{
    Background = SolidColorBrush(Color::FromRgb(0x17, 0x12, 0x0B));
    // SnapsToDevicePixels = true: no xOS a escala é 1, os pixels já caem inteiros
}

InstrumentoShader::~InstrumentoShader()
{
    // a imagem do NanoVG foi criada com NVG_IMAGE_NODELETE: a textura é nossa
    if (_imagemNvg && Application::Vg()) nvgDeleteImage(Application::Vg(), _imagemNvg);
    if (_fbo) glDeleteFramebuffers(1, &_fbo);
    if (_texAlvo) glDeleteTextures(1, &_texAlvo);
    if (_texDados) glDeleteTextures(1, &_texDados);
    if (_vbo) glDeleteBuffers(1, &_vbo);
    if (_programa) glDeleteProgram(_programa);
}

bool InstrumentoShader::Montar()
{
    if (_programa != 0) return true;
    if (!Falha.empty()) return false;

    // LINUX: o original conferia RenderCapability.IsPixelShaderVersionSupported(3, 0) (área de
    // trabalho remota, driver genérico). No GLES 2 todo contexto roda fragment shader; o que pode
    // faltar é o contexto (nunca aberto): aí não há placa.
    if (!Application::Vg())
    {
        Falha = "a placa não está aberta (sem contexto OpenGL ES)";
        return false;
    }

    std::string erros;
    GLuint vs = Compila(GL_VERTEX_SHADER, VertexShader, erros);
    GLuint fs = vs ? Compila(GL_FRAGMENT_SHADER, std::string(Cabecalho) + Glsl(), erros) : 0;
    if (!vs || !fs)
    {
        if (vs) glDeleteShader(vs);
        Falha = "O shader não compilou: " + (erros.empty() ? std::string("sem detalhe") : erros);
        return false;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "pos");
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[1024] = {};
        glGetProgramInfoLog(prog, sizeof log - 1, nullptr, log);
        glDeleteProgram(prog);
        // no original: "O WPF recusou o shader: "
        Falha = std::string("O driver recusou o shader: ") + (log[0] ? log : "sem detalhe");
        return false;
    }

    _tira.assign((size_t) Colunas() * Linhas() * 4, 0);
    for (size_t i = 3; i < _tira.size(); i += 4) _tira[i] = 255;

    // A textura de dados. Vizinho mais próximo: interpolar entre raias do espectro inventaria
    // valores que não foram medidos.
    GLint texAntes = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texAntes);
    glGenTextures(1, &_texDados);
    glBindTexture(GL_TEXTURE_2D, _texDados);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, Colunas(), Linhas(), 0, GL_RGBA, GL_UNSIGNED_BYTE, _tira.data());
    glBindTexture(GL_TEXTURE_2D, (GLuint) texAntes);

    const float quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    GLint vboAntes = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &vboAntes);
    glGenBuffers(1, &_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint) vboAntes);

    _programa = prog;
    _locDados = glGetUniformLocation(prog, "dados");
    _locQuadro = glGetUniformLocation(prog, "quadro");
    if (glGetError() != GL_NO_ERROR)
    {
        Falha = "O driver recusou o shader: erro do OpenGL ao preparar a textura";
        return false;
    }
    return true;
}

void InstrumentoShader::Definir(int coluna, int linha, uint8_t r, uint8_t g, uint8_t b)
{
    if (coluna < 0 || coluna >= Colunas() || linha < 0 || linha >= Linhas()) return;
    uint8_t *p = &_tira[((size_t) linha * Colunas() + coluna) * 4];
    p[0] = r; p[1] = g; p[2] = b; p[3] = 0xFF;
}

uint8_t InstrumentoShader::Byte(double v)
{
    if (v <= 0) return 0;
    if (v >= 1) return 255;
    return (uint8_t) (v * 255.0);
}

void InstrumentoShader::Atualizar(Modelo::AnalisadorAudio &a, const std::vector<float> &esq, const std::vector<float> &dir)
{
    if (_programa == 0) return;
    Preencher(a, esq, dir);
    // _dados.WritePixels + _efeito.Quadro = ++_quadro (força o redesenho)
    // LINUX: a tira sobe para a placa no próximo desenho (OnRender), no fio da tela
    _quadro += 1.0;
    _sujo = true;
    InvalidateVisual();
}

void InstrumentoShader::OnRender(DrawingContext &dc)
{
    Border::OnRender(dc);
    if (_programa == 0) return;
    int w = (int) std::lround(ActualWidth), h = (int) std::lround(ActualHeight);
    if (w < 1 || h < 1) return;

    // Tudo o que o NanoVG guarda do estado do GL volta como estava: ele desenha o quadro inteiro
    // só no fim (nvgEndFrame), e o que fazemos aqui roda antes, direto na placa.
    GLint fboAntes = 0, texAntes = 0, vboAntes = 0, progAntes = 0, unidadeAntes = 0;
    GLint vista[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fboAntes);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &unidadeAntes);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texAntes);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &vboAntes);
    glGetIntegerv(GL_CURRENT_PROGRAM, &progAntes);
    glGetIntegerv(GL_VIEWPORT, vista);
    GLboolean blend = glIsEnabled(GL_BLEND), corte = glIsEnabled(GL_SCISSOR_TEST), faces = glIsEnabled(GL_CULL_FACE),
              fundo = glIsEnabled(GL_DEPTH_TEST), estencil = glIsEnabled(GL_STENCIL_TEST);

    bool novoAlvo = w != _larguraAlvo || h != _alturaAlvo || _fbo == 0;
    if (novoAlvo)
    {
        if (_imagemNvg) nvgDeleteImage(dc.vg, _imagemNvg), _imagemNvg = 0;
        if (_fbo) glDeleteFramebuffers(1, &_fbo), _fbo = 0;
        if (_texAlvo) glDeleteTextures(1, &_texAlvo), _texAlvo = 0;
        glGenTextures(1, &_texAlvo);
        glBindTexture(GL_TEXTURE_2D, _texAlvo);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _texAlvo, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint) fboAntes);
            glBindTexture(GL_TEXTURE_2D, (GLuint) texAntes);
            glActiveTexture((GLenum) unidadeAntes);
            return;
        }
        _larguraAlvo = w;
        _alturaAlvo = h;
        // O FBO guarda a linha de baixo primeiro; o NanoVG lê a de cima: FLIPY
        _imagemNvg = nvglCreateImageFromHandleGLES2(dc.vg, _texAlvo, w, h, NVG_IMAGE_FLIPY | NVG_IMAGE_NODELETE);
        _sujo = true;
    }

    if (_sujo)
    {
        _sujo = false;
        glBindTexture(GL_TEXTURE_2D, _texDados);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, Colunas(), Linhas(), GL_RGBA, GL_UNSIGNED_BYTE, _tira.data());
        glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
        glViewport(0, 0, w, h);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glUseProgram(_programa);
        glUniform1i(_locDados, 0);
        if (_locQuadro >= 0) glUniform1f(_locQuadro, (float) _quadro);
        glBindBuffer(GL_ARRAY_BUFFER, _vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(0);
    }

    // de volta ao que o NanoVG e a janela esperam
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint) fboAntes);
    glViewport(vista[0], vista[1], vista[2], vista[3]);
    glUseProgram((GLuint) progAntes);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint) vboAntes);
    glBindTexture(GL_TEXTURE_2D, (GLuint) texAntes);
    glActiveTexture((GLenum) unidadeAntes);
    if (blend) glEnable(GL_BLEND);
    if (corte) glEnable(GL_SCISSOR_TEST);
    if (faces) glEnable(GL_CULL_FACE);
    if (fundo) glEnable(GL_DEPTH_TEST);
    if (estencil) glEnable(GL_STENCIL_TEST);

    if (_imagemNvg)
    {
        Rect r = Limites;
        NVGpaint p = nvgImagePattern(dc.vg, (float) r.X, (float) r.Y, (float) w, (float) h, 0, _imagemNvg, 1.0f);
        nvgBeginPath(dc.vg);
        nvgRect(dc.vg, (float) r.X, (float) r.Y, (float) w, (float) h);
        nvgFillPaint(dc.vg, p);
        nvgFill(dc.vg);
    }
}

} // namespace ARCA::Vistas
