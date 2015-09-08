// Copyright (c) Microsoft Corporation. All rights reserved.
//
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.

#pragma once

namespace ABI { namespace Microsoft { namespace Graphics { namespace Canvas { namespace Effects
{
    using namespace ::Microsoft::WRL;
    using namespace ABI::Windows::Foundation;
    using namespace ABI::Windows::Foundation::Collections;
    using namespace ABI::Windows::UI;
    using namespace ABI::Microsoft::Graphics::Canvas;
    using namespace ::collections;

    struct EffectPropertyMapping
    {
        LPCWSTR Name;
        UINT Index;
        GRAPHICS_EFFECT_PROPERTY_MAPPING Mapping;
    };

    struct EffectPropertyMappingTable
    {
        EffectPropertyMapping const* Mappings;
        size_t Count;
    };

    class CanvasEffect
        : public Implements<
            RuntimeClassFlags<WinRtClassicComMix>,
            IGraphicsEffect,
            IGraphicsEffectSource,
            IGraphicsEffectD2D1Interop,
            ICanvasImageInternal,
            ICanvasImage,
            ABI::Windows::Foundation::IClosable>,
          private LifespanTracker<CanvasEffect>
    {

    private:
        ComPtr<ID2D1Effect> m_resource;

        // Unlike other objects, !m_resource does not necessarily indicate
        // that the object was closed.
        bool m_closed; 

        IID m_effectId;

        std::vector<ComPtr<IPropertyValue>> m_properties;
        bool m_propertiesChanged;

        ComPtr<Vector<IGraphicsEffectSource*>> m_sources;

        ComPtr<IPropertyValueStatics> m_propertyValueFactory;

        ComPtr<IUnknown> m_previousDeviceIdentity;
        std::vector<uint64_t> m_previousSourceRealizationIds;
        uint64_t m_realizationId;
        
        std::vector<ComPtr<ID2D1Effect>> m_dpiCompensators;

        bool m_insideGetImage;

        WinString m_name;

    public:
        //
        // IClosable
        //

        IFACEMETHOD(Close)() override;

        //
        // IGraphicsEffect
        //

        IFACEMETHOD(get_Name)(HSTRING* name) override;
        IFACEMETHOD(put_Name)(HSTRING name) override;

        //
        // IGraphicsEffectD2D1Interop
        //

        IFACEMETHOD(GetEffectId)(GUID* id) override;
        IFACEMETHOD(GetSourceCount)(UINT* count) override;
        IFACEMETHOD(GetSource)(UINT index, IGraphicsEffectSource** source) override;
        IFACEMETHOD(GetPropertyCount)(UINT* count) override;
        IFACEMETHOD(GetProperty)(UINT index, IPropertyValue** value) override;
        IFACEMETHOD(GetNamedPropertyMapping)(LPCWSTR name, UINT* index, GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) override;

        //
        // Not part of IGraphicsEffectD2D1Interop, but same semantics as if it was a peer to GetSource.
        //

        STDMETHOD(SetSource)(UINT index, IGraphicsEffectSource* source);

        //
        // ICanvasImage
        //

        IFACEMETHOD(GetBounds)(
            ICanvasDrawingSession *drawingSession,
            Rect *bounds) override;

        IFACEMETHOD(GetBoundsWithTransform)(
            ICanvasDrawingSession *drawingSession,
            Numerics::Matrix3x2 transform,
            Rect *bounds) override;

        //
        // ICanvasImageInternal
        //

        ComPtr<ID2D1Image> GetD2DImage(ID2D1DeviceContext* deviceContext) override;
        RealizedEffectNode GetRealizedEffectNode(ID2D1DeviceContext* deviceContext, float targetDpi) override;

    protected:
        // for effects with unknown number of sources, sourcesSize has to be zero
        CanvasEffect(ID2D1Effect* effect, IID const& m_effectId, unsigned int propertiesSize, unsigned int sourcesSize, bool isSourcesSizeFixed);

        virtual ~CanvasEffect() = default;

        ComPtr<Vector<IGraphicsEffectSource*>> const& Sources() { return m_sources; }

        virtual EffectPropertyMappingTable GetPropertyMapping()          { return EffectPropertyMappingTable{ nullptr, 0 }; }
        virtual EffectPropertyMappingTable GetPropertyMappingHandCoded() { return EffectPropertyMappingTable{ nullptr, 0 }; }


        //
        // The main property set/get methods. TBoxed is how we represent the data internally,
        // while TPublic is how it is exposed by strongly typed effect subclasses. For instance
        // enums are stored as unsigned integers, vectors and matrices as float arrays, and
        // colors as float[3] or float[4] depending on whether they include alpha.
        //

        template<typename TBoxed, typename TPublic>
        void SetBoxedProperty(unsigned int index, TPublic const& value)
        {
            assert(index < m_properties.size());

            m_properties[index] = PropertyTypeConverter<TBoxed, TPublic>::Box(m_propertyValueFactory.Get(), value);
            m_propertiesChanged = true;
        }

        template<typename TBoxed, typename TPublic>
        void GetBoxedProperty(unsigned int index, TPublic* value)
        {
            assert(index < m_properties.size());

            CheckInPointer(value);

            PropertyTypeConverter<TBoxed, TPublic>::Unbox(m_properties[index].Get(), value);
        }

        template<typename T>
        void SetArrayProperty(unsigned int index, uint32_t valueCount, T const* value)
        {
            assert(index < m_properties.size());

            m_properties[index] = CreateProperty(m_propertyValueFactory.Get(), valueCount, value);
            m_propertiesChanged = true;
        }

        template<typename T>
        void SetArrayProperty(unsigned int index, std::initializer_list<T> const& value)
        {
            SetArrayProperty<T>(index, static_cast<uint32_t>(value.size()), value.begin());
        }

        template<typename T>
        void GetArrayProperty(unsigned int index, uint32_t* valueCount, T** value)
        {
            assert(index < m_properties.size());

            CheckInPointer(valueCount);
            CheckAndClearOutPointer(value);

            GetValueOfProperty(m_properties[index].Get(), valueCount, value);
        }


        // Marker types, used as TBoxed for values that need special conversion.
        struct ConvertRadiansToDegrees { };
        struct ConvertAlphaMode { };


    private:
        void SetD2DInputs(ID2D1DeviceContext* deviceContext, float targetDpi, bool wasRecreated);
        void SetD2DProperties();

        void ThrowIfClosed();


        //
        // PropertyTypeConverter is responsible for converting values between TBoxed and TPublic forms.
        // This is designed to produce compile errors if incompatible types are specified.
        //

        template<typename TBoxed, typename TPublic, typename Enable = void>
        struct PropertyTypeConverter
        {
            static_assert(std::is_same<TBoxed, TPublic>::value, "Default PropertyTypeConverter should only be used when TBoxed = TPublic");

            static ComPtr<IPropertyValue> Box(IPropertyValueStatics* factory, TPublic const& value)
            {
                return CreateProperty(factory, value);
            }

            static void Unbox(IPropertyValue* propertyValue, TPublic* result)
            {
                GetValueOfProperty(propertyValue, result);
            }
        };


        // Enum values are boxed as unsigned integers.
        template<typename TPublic>
        struct PropertyTypeConverter<uint32_t, TPublic,
                                     typename std::enable_if<std::is_enum<TPublic>::value>::type>
        {
            static ComPtr<IPropertyValue> Box(IPropertyValueStatics* factory, TPublic value)
            {
                return CreateProperty(factory, static_cast<uint32_t>(value));
            }

            static void Unbox(IPropertyValue* propertyValue, TPublic* result)
            {
                uint32_t value;
                GetValueOfProperty(propertyValue, &value);
                *result = static_cast<TPublic>(value);
            }
        };


        // Vectors and matrices are boxed as float arrays.
        template<int N, typename TPublic>
        struct PropertyTypeConverter<float[N], TPublic>
        {
            static_assert(std::is_same<TPublic, Numerics::Vector2>::value   ||
                          std::is_same<TPublic, Numerics::Vector3>::value   ||
                          std::is_same<TPublic, Numerics::Vector4>::value   ||
                          std::is_same<TPublic, Numerics::Matrix3x2>::value ||
                          std::is_same<TPublic, Numerics::Matrix4x4>::value ||
                          std::is_same<TPublic, Matrix5x4>::value,
                          "This type cannot be boxed as a float array");

            static_assert(sizeof(TPublic) == sizeof(float[N]), "Wrong array size");

            static ComPtr<IPropertyValue> Box(IPropertyValueStatics* factory, TPublic const& value)
            {
                ComPtr<IPropertyValue> propertyValue;
                ThrowIfFailed(factory->CreateSingleArray(N, (float*)&value, &propertyValue));
                return propertyValue;
            }

            static void Unbox(IPropertyValue* propertyValue, TPublic* result)
            {
                ComArray<float> value;

                ThrowIfFailed(propertyValue->GetSingleArray(value.GetAddressOfSize(), value.GetAddressOfData()));

                if (value.GetSize() != N)
                    ThrowHR(E_BOUNDS);

                *result = *reinterpret_cast<TPublic*>(value.GetData());
            }
        };


        // Color can be boxed as a float4 (for properties that include alpha).
        template<>
        struct PropertyTypeConverter<float[4], Color>
        {
            typedef PropertyTypeConverter<float[4], Numerics::Vector4> VectorConverter;

            static ComPtr<IPropertyValue> Box(IPropertyValueStatics* factory, Color const& value)
            {
                return VectorConverter::Box(factory, ToVector4(value));
            }

            static void Unbox(IPropertyValue* propertyValue, Color* result)
            {
                Numerics::Vector4 value;
                VectorConverter::Unbox(propertyValue, &value);
                *result = ToWindowsColor(value);
            }
        };


        // Color can also be boxed as float3 (for properties that only use rgb).
        template<>
        struct PropertyTypeConverter<float[3], Color>
        {
            typedef PropertyTypeConverter<float[3], Numerics::Vector3> VectorConverter;

            static ComPtr<IPropertyValue> Box(IPropertyValueStatics* factory, Color const& value)
            {
                return VectorConverter::Box(factory, ToVector3(value));
            }

            static void Unbox(IPropertyValue* propertyValue, Color* result)
            {
                Numerics::Vector3 value;
                VectorConverter::Unbox(propertyValue, &value);
                *result = ToWindowsColor(value);
            }
        };


        // Rect is boxed as a float4, after converting WinRT x/y/w/h format to D2D left/top/right/bottom.
        template<>
        struct PropertyTypeConverter<float[4], Rect>
        {
            typedef PropertyTypeConverter<float[4], Numerics::Vector4> VectorConverter;

            static ComPtr<IPropertyValue> Box(IPropertyValueStatics* factory, Rect const& value)
            {
                auto d2dRect = ToD2DRect(value);
                return VectorConverter::Box(factory, *ReinterpretAs<Numerics::Vector4*>(&d2dRect));
            }

            static void Unbox(IPropertyValue* propertyValue, Rect* result)
            {
                Numerics::Vector4 value;
                VectorConverter::Unbox(propertyValue, &value);
                *result = FromD2DRect(*ReinterpretAs<D2D1_RECT_F*>(&value));
            }
        };


        // Handle the conversion for angles that are exposed as radians in WinRT, while internally D2D uses degrees.
        template<>
        struct PropertyTypeConverter<ConvertRadiansToDegrees, float>
        {
            static ComPtr<IPropertyValue> Box(IPropertyValueStatics* factory, float value)
            {
                return CreateProperty(factory, ::DirectX::XMConvertToDegrees(value));
            }

            static void Unbox(IPropertyValue* propertyValue, float* result)
            {
                float degrees;
                GetValueOfProperty(propertyValue, &degrees);
                *result = ::DirectX::XMConvertToRadians(degrees);
            }
        };


        // Handle the conversion between CanvasAlphaMode and D2D1_ALPHA_MODE/D2D1_COLORMATRIX_ALPHA_MODE enum values.
        template<>
        struct PropertyTypeConverter<ConvertAlphaMode, CanvasAlphaMode>
        {
            static_assert(D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED == D2D1_ALPHA_MODE_PREMULTIPLIED, "Enum values should match");
            static_assert(D2D1_COLORMATRIX_ALPHA_MODE_STRAIGHT == D2D1_ALPHA_MODE_STRAIGHT, "Enum values should match");

            static ComPtr<IPropertyValue> Box(IPropertyValueStatics* factory, CanvasAlphaMode value)
            {
                if (value == CanvasAlphaMode::Ignore)
                    ThrowHR(E_INVALIDARG);

                return CreateProperty(factory, static_cast<uint32_t>(ToD2DAlphaMode(value)));
            }

            static void Unbox(IPropertyValue* propertyValue, CanvasAlphaMode* result)
            {
                uint32_t value;
                GetValueOfProperty(propertyValue, &value);
                *result = FromD2DAlphaMode(static_cast<D2D1_ALPHA_MODE>(value));
            }
        };


        //
        // Wrap the IPropertyValue accessors (which use different method names for each type) with
        // overloaded C++ versions that can be used by generic PropertyTypeConverter implementations.
        //

#define PROPERTY_TYPE_ACCESSOR(TYPE, WINRT_NAME)                                                        \
        static ComPtr<IPropertyValue> CreateProperty(IPropertyValueStatics* factory, TYPE const& value) \
        {                                                                                               \
            ComPtr<IPropertyValue> propertyValue;                                                       \
            ThrowIfFailed(factory->Create##WINRT_NAME(value, &propertyValue));                          \
            return propertyValue;                                                                       \
        }                                                                                               \
                                                                                                        \
        static void GetValueOfProperty(IPropertyValue* propertyValue, TYPE* result)                     \
        {                                                                                               \
            ThrowIfFailed(propertyValue->Get##WINRT_NAME(result));                                      \
        }

#define ARRAY_PROPERTY_TYPE_ACCESSOR(TYPE, WINRT_NAME)                                                                          \
        static ComPtr<IPropertyValue> CreateProperty(IPropertyValueStatics* factory, uint32_t valueCount, TYPE const* value)    \
        {                                                                                                                       \
            ComPtr<IPropertyValue> propertyValue;                                                                               \
            ThrowIfFailed(factory->Create##WINRT_NAME##Array(valueCount, const_cast<TYPE*>(value), &propertyValue));            \
            return propertyValue;                                                                                               \
        }                                                                                                                       \
                                                                                                                                \
        static void GetValueOfProperty(IPropertyValue* propertyValue, uint32_t* valueCount, TYPE** value)                       \
        {                                                                                                                       \
            ThrowIfFailed(propertyValue->Get##WINRT_NAME##Array(valueCount, value));                                            \
        }

        PROPERTY_TYPE_ACCESSOR(float,    Single)
        PROPERTY_TYPE_ACCESSOR(int32_t,  Int32)
        PROPERTY_TYPE_ACCESSOR(uint32_t, UInt32)
        PROPERTY_TYPE_ACCESSOR(boolean,  Boolean)
        
        ARRAY_PROPERTY_TYPE_ACCESSOR(float, Single)

#undef PROPERTY_TYPE_ACCESSOR
#undef ARRAY_PROPERTY_TYPE_ACCESSOR


        //
        // Macros used by the generated strongly typed effect subclasses
        // 

#define EFFECT_PROPERTY(NAME, TYPE)                                                     \
        IFACEMETHOD(get_##NAME)(TYPE* value) override;                                  \
        IFACEMETHOD(put_##NAME)(TYPE value) override

#define EFFECT_ARRAY_PROPERTY(NAME, TYPE)                                               \
        IFACEMETHOD(get_##NAME)(UINT32 *valueCount, TYPE **valueElements) override;     \
        IFACEMETHOD(put_##NAME)(UINT32 valueCount, TYPE *valueElements) override

#define EFFECT_SOURCES_PROPERTY()                                                       \
        IFACEMETHOD(get_Sources)(IVector<IGraphicsEffectSource*>** value) override


#define IMPLEMENT_EFFECT_PROPERTY(CLASS, PROPERTY, BOXED_TYPE, PUBLIC_TYPE, INDEX)      \
                                                                                        \
        IFACEMETHODIMP CLASS::get_##PROPERTY(_Out_ PUBLIC_TYPE* value)                  \
        {                                                                               \
            return ExceptionBoundary([&]                                                \
            {                                                                           \
                GetBoxedProperty<BOXED_TYPE, PUBLIC_TYPE>(INDEX, value);                \
            });                                                                         \
        }                                                                               \
                                                                                        \
        IFACEMETHODIMP CLASS::put_##PROPERTY(_In_ PUBLIC_TYPE value)                    \
        {                                                                               \
            return ExceptionBoundary([&]                                                \
            {                                                                           \
                SetBoxedProperty<BOXED_TYPE, PUBLIC_TYPE>(INDEX, value);                \
            });                                                                         \
        }


#define IMPLEMENT_EFFECT_PROPERTY_WITH_VALIDATION(CLASS, PROPERTY, BOXED_TYPE,          \
                                                  PUBLIC_TYPE, INDEX, VALIDATOR)        \
                                                                                        \
        IFACEMETHODIMP CLASS::get_##PROPERTY(_Out_ PUBLIC_TYPE* value)                  \
        {                                                                               \
            return ExceptionBoundary([&]                                                \
            {                                                                           \
                GetBoxedProperty<BOXED_TYPE, PUBLIC_TYPE>(INDEX, value);                \
            });                                                                         \
        }                                                                               \
                                                                                        \
        IFACEMETHODIMP CLASS::put_##PROPERTY(_In_ PUBLIC_TYPE value)                    \
        {                                                                               \
            if (!(VALIDATOR))                                                           \
            {                                                                           \
                return E_INVALIDARG;                                                    \
            }                                                                           \
                                                                                        \
            return ExceptionBoundary([&]                                                \
            {                                                                           \
                SetBoxedProperty<BOXED_TYPE, PUBLIC_TYPE>(INDEX, value);                \
            });                                                                         \
        }
    };


#define IMPLEMENT_EFFECT_ARRAY_PROPERTY(CLASS, PROPERTY, TYPE, INDEX)                   \
                                                                                        \
        IFACEMETHODIMP CLASS::get_##PROPERTY(UINT32 *valueCount, TYPE **valueElements)  \
        {                                                                               \
            return ExceptionBoundary([&]                                                \
            {                                                                           \
                GetArrayProperty<TYPE>(INDEX, valueCount, valueElements);               \
            });                                                                         \
        }                                                                               \
                                                                                        \
        IFACEMETHODIMP CLASS::put_##PROPERTY(UINT32 valueCount, TYPE *valueElements)    \
        {                                                                               \
            return ExceptionBoundary([&]                                                \
            {                                                                           \
                SetArrayProperty<TYPE>(INDEX, valueCount, valueElements);               \
            });                                                                         \
        }


#define IMPLEMENT_EFFECT_SOURCE_PROPERTY(CLASS, SOURCE_NAME, SOURCE_INDEX)              \
                                                                                        \
        IFACEMETHODIMP CLASS::get_##SOURCE_NAME(_Out_ IGraphicsEffectSource** source)   \
        {                                                                               \
            return GetSource(SOURCE_INDEX, source);                                     \
        }                                                                               \
                                                                                        \
        IFACEMETHODIMP CLASS::put_##SOURCE_NAME(_In_ IGraphicsEffectSource* source)     \
        {                                                                               \
            return SetSource(SOURCE_INDEX, source);                                     \
        }


#define IMPLEMENT_EFFECT_SOURCES_PROPERTY(CLASS)                                        \
                                                                                        \
        IFACEMETHODIMP CLASS::get_Sources(_Out_ IVector<IGraphicsEffectSource*>** value)\
        {                                                                               \
            return ExceptionBoundary([&]                                                \
            {                                                                           \
                CheckAndClearOutPointer(value);                                         \
                ThrowIfFailed(Sources().CopyTo(value));                                 \
            });                                                                         \
        }


#define EFFECT_PROPERTY_MAPPING()                                                       \
    EffectPropertyMappingTable GetPropertyMapping() override

#define EFFECT_PROPERTY_MAPPING_HANDCODED()                                             \
    EffectPropertyMappingTable GetPropertyMappingHandCoded() override


#define IMPLEMENT_EFFECT_PROPERTY_MAPPING(CLASS, ...)                                   \
    IMPLEMENT_EFFECT_PROPERTY_MAPPING_WORKER(CLASS, PropertyMapping, __VA_ARGS__)

#define IMPLEMENT_EFFECT_PROPERTY_MAPPING_HANDCODED(CLASS, ...)                         \
    IMPLEMENT_EFFECT_PROPERTY_MAPPING_WORKER(CLASS, PropertyMappingHandCoded, __VA_ARGS__)


#define IMPLEMENT_EFFECT_PROPERTY_MAPPING_WORKER(CLASS, SUFFIX, ...)                    \
    static const EffectPropertyMapping CLASS##SUFFIX[] =                                \
    {                                                                                   \
        __VA_ARGS__                                                                     \
    };                                                                                  \
                                                                                        \
    EffectPropertyMappingTable CLASS::Get##SUFFIX()                                     \
    {                                                                                   \
        auto count = sizeof(CLASS##SUFFIX) / sizeof(EffectPropertyMapping);             \
        return EffectPropertyMappingTable{ CLASS##SUFFIX, count };                      \
    }

}}}}}
