#include "Renderer/GpuFrameTimer.h"

#include <glad/glad.h>

namespace GeoFPS
{
void GpuFrameTimer::Poll()
{
    if (!m_Initialized)
    {
        m_Available = GLAD_GL_VERSION_3_3 != 0;
        if (!m_Available)
        {
            return;
        }
        glGenQueries(static_cast<GLsizei>(m_Queries.size()), m_Queries.data());
        m_Initialized = true;
    }

    for (size_t index = 0; index < m_Queries.size(); ++index)
    {
        if (!m_Pending[index])
        {
            continue;
        }

        GLuint available = GL_FALSE;
        glGetQueryObjectuiv(m_Queries[index], GL_QUERY_RESULT_AVAILABLE, &available);
        if (available == GL_TRUE)
        {
            GLuint64 elapsedNs = 0;
            glGetQueryObjectui64v(m_Queries[index], GL_QUERY_RESULT, &elapsedNs);
            m_LastFrameMs = static_cast<float>(static_cast<double>(elapsedNs) / 1000000.0);
            m_Pending[index] = false;
        }
    }
}

void GpuFrameTimer::Begin()
{
    if (!m_Initialized || m_Pending[static_cast<size_t>(m_WriteIndex)])
    {
        m_Active = false;
        return;
    }

    glBeginQuery(GL_TIME_ELAPSED, m_Queries[static_cast<size_t>(m_WriteIndex)]);
    m_Active = true;
}

void GpuFrameTimer::End()
{
    if (!m_Active)
    {
        return;
    }

    glEndQuery(GL_TIME_ELAPSED);
    m_Pending[static_cast<size_t>(m_WriteIndex)] = true;
    m_WriteIndex = (m_WriteIndex + 1) % static_cast<int>(m_Queries.size());
    m_Active = false;
}

void GpuFrameTimer::Shutdown()
{
    if (m_Initialized)
    {
        glDeleteQueries(static_cast<GLsizei>(m_Queries.size()), m_Queries.data());
        m_Queries = {};
        m_Pending = {};
        m_Initialized = false;
    }
}
} // namespace GeoFPS
