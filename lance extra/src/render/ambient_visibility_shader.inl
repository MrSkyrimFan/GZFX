        std::string ShaderSource()
        {
            std::string source;
            source.reserve(24000);

#include "ambient_visibility_hlsl_1.inl"
#include "ambient_visibility_hlsl_2.inl"
#include "ambient_visibility_hlsl_3.inl"
#include "ambient_visibility_hlsl_4.inl"

            return source;
        }

