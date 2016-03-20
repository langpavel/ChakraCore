//-------------------------------------------------------------------------------------------------------
// Copyright (C) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------
#include "RuntimeDebugPch.h"

#if ENABLE_TTD

namespace TTD
{
    InflateMap::InflateMap()
        : m_typeMap(), m_handlerMap(),
        m_tagToGlobalObjectMap(), m_objectMap(),
        m_functionBodyMap(), m_environmentMap(), m_slotArrayMap(), m_promiseDataMap(&HeapAllocator::Instance),
        m_inflatePinSet(nullptr), m_environmentPinSet(nullptr), m_slotArrayPinSet(nullptr), m_oldInflatePinSet(nullptr),
        m_oldObjectMap(), m_oldFunctionBodyMap(), m_propertyReset(&HeapAllocator::Instance)
    {
        ;
    }

    InflateMap::~InflateMap()
    {
        if(this->m_inflatePinSet != nullptr)
        {
            this->m_inflatePinSet->GetAllocator()->RootRelease(this->m_inflatePinSet);
            this->m_inflatePinSet = nullptr;
        }

        if(this->m_environmentPinSet != nullptr)
        {
            this->m_environmentPinSet->GetAllocator()->RootRelease(this->m_environmentPinSet);
            this->m_environmentPinSet = nullptr;
        }

        if(this->m_slotArrayPinSet != nullptr)
        {
            this->m_slotArrayPinSet->GetAllocator()->RootRelease(this->m_slotArrayPinSet);
            this->m_slotArrayPinSet = nullptr;
        }

        if(this->m_oldInflatePinSet != nullptr)
        {
            this->m_oldInflatePinSet->GetAllocator()->RootRelease(this->m_oldInflatePinSet);
            this->m_oldInflatePinSet = nullptr;
        }
    }

    void InflateMap::PrepForInitialInflate(ThreadContext* threadContext, uint32 ctxCount, uint32 handlerCount, uint32 typeCount, uint32 objectCount, uint32 bodyCount, uint32 envCount, uint32 slotCount)
    {
        this->m_typeMap.Initialize(typeCount);
        this->m_handlerMap.Initialize(handlerCount);
        this->m_tagToGlobalObjectMap.Initialize(ctxCount);
        this->m_objectMap.Initialize(objectCount);
        this->m_functionBodyMap.Initialize(bodyCount);
        this->m_environmentMap.Initialize(envCount);
        this->m_slotArrayMap.Initialize(slotCount);
        this->m_promiseDataMap.Clear();

        this->m_inflatePinSet = RecyclerNew(threadContext->GetRecycler(), ObjectPinSet, threadContext->GetRecycler(), objectCount);
        threadContext->GetRecycler()->RootAddRef(this->m_inflatePinSet);

        this->m_environmentPinSet = RecyclerNew(threadContext->GetRecycler(), EnvironmentPinSet, threadContext->GetRecycler(), objectCount);
        threadContext->GetRecycler()->RootAddRef(this->m_environmentPinSet);

        this->m_slotArrayPinSet = RecyclerNew(threadContext->GetRecycler(), SlotArrayPinSet, threadContext->GetRecycler(), objectCount);
        threadContext->GetRecycler()->RootAddRef(this->m_slotArrayPinSet);
    }

    void InflateMap::PrepForReInflate(uint32 ctxCount, uint32 handlerCount, uint32 typeCount, uint32 objectCount, uint32 bodyCount, uint32 envCount, uint32 slotCount)
    {
        this->m_typeMap.Initialize(typeCount);
        this->m_handlerMap.Initialize(handlerCount);
        this->m_tagToGlobalObjectMap.Initialize(ctxCount);
        this->m_environmentMap.Initialize(envCount);
        this->m_slotArrayMap.Initialize(slotCount);
        this->m_promiseDataMap.Clear();

        //We re-use these values (and reset things below) so we don't neet to initialize them here
        //m_objectMap
        //m_functionBodyMap

        //copy info we want to reuse into the old maps
        this->m_oldObjectMap.MoveDataInto(this->m_objectMap);
        this->m_oldFunctionBodyMap.MoveDataInto(this->m_functionBodyMap);

        //allocate the old pin set and fill it
        AssertMsg(this->m_oldInflatePinSet == nullptr, "Old pin set is not null.");
        Recycler* pinRecycler = this->m_inflatePinSet->GetAllocator();
        this->m_oldInflatePinSet = RecyclerNew(pinRecycler, ObjectPinSet, pinRecycler, this->m_inflatePinSet->Count());
        pinRecycler->RootAddRef(this->m_oldInflatePinSet);

        for(auto iter = this->m_inflatePinSet->GetIterator(); iter.IsValid(); iter.MoveNext())
        {
            this->m_oldInflatePinSet->AddNew(iter.CurrentKey());
        }

        this->m_inflatePinSet->Clear();
        this->m_environmentPinSet->Clear();
        this->m_slotArrayPinSet->Clear();
    }

    void InflateMap::CleanupAfterInflate()
    {
        this->m_handlerMap.Unload();
        this->m_typeMap.Unload();
        this->m_tagToGlobalObjectMap.Unload();
        this->m_environmentMap.Unload();
        this->m_slotArrayMap.Unload();
        this->m_promiseDataMap.Clear();

        //We re-use these values (and reset things later) so we don't want to unload them here
        //m_objectMap
        //m_functionBodyMap

        this->m_oldObjectMap.Unload();
        this->m_oldFunctionBodyMap.Unload();
        this->m_propertyReset.Clear();

        if(this->m_oldInflatePinSet != nullptr)
        {
            this->m_oldInflatePinSet->GetAllocator()->RootRelease(this->m_oldInflatePinSet);
            this->m_oldInflatePinSet = nullptr;
        }
    }

    bool InflateMap::IsObjectAlreadyInflated(TTD_PTR_ID objid) const
    {
        return this->m_objectMap.Contains(objid);
    }

    bool InflateMap::IsFunctionBodyAlreadyInflated(TTD_PTR_ID fbodyid) const
    {
        return this->m_functionBodyMap.Contains(fbodyid);
    }

    Js::RecyclableObject* InflateMap::FindReusableObjectIfExists(TTD_PTR_ID objid) const
    {
        if(!this->m_oldObjectMap.IsValid())
        {
            return nullptr;
        }
        {
            return this->m_oldObjectMap.LookupKnownItem(objid);
        }
    }

    Js::FunctionBody* InflateMap::FindReusableFunctionBodyIfExists(TTD_PTR_ID fbodyid) const
    {
        if(!this->m_oldFunctionBodyMap.IsValid())
        {
            return nullptr;
        }
        else
        {
            return this->m_oldFunctionBodyMap.LookupKnownItem(fbodyid);
        }
    }

    Js::DynamicTypeHandler* InflateMap::LookupHandler(TTD_PTR_ID handlerId) const
    {
        return this->m_handlerMap.LookupKnownItem(handlerId);
    }

    Js::Type* InflateMap::LookupType(TTD_PTR_ID typeId) const
    {
        return this->m_typeMap.LookupKnownItem(typeId);
    }

    Js::ScriptContext* InflateMap::LookupScriptContext(TTD_LOG_TAG sctag) const
    {
        return this->m_tagToGlobalObjectMap.LookupKnownItem(sctag)->GetScriptContext();
    }

    Js::RecyclableObject* InflateMap::LookupObject(TTD_PTR_ID objid) const
    {
        return this->m_objectMap.LookupKnownItem(objid);
    }

    Js::FunctionBody* InflateMap::LookupFunctionBody(TTD_PTR_ID functionId) const
    {
        return this->m_functionBodyMap.LookupKnownItem(functionId);
    }

    Js::FrameDisplay* InflateMap::LookupEnvironment(TTD_PTR_ID envid) const
    {
        return this->m_environmentMap.LookupKnownItem(envid);
    }

    Js::Var* InflateMap::LookupSlotArray(TTD_PTR_ID slotid) const
    {
        return this->m_slotArrayMap.LookupKnownItem(slotid);
    }

    void InflateMap::AddDynamicHandler(TTD_PTR_ID handlerId, Js::DynamicTypeHandler* value)
    {
        this->m_handlerMap.AddItem(handlerId, value);
    }

    void InflateMap::AddType(TTD_PTR_ID typeId, Js::Type* value)
    {
        this->m_typeMap.AddItem(typeId, value);
    }

    void InflateMap::AddScriptContext(TTD_LOG_TAG sctag, Js::ScriptContext* value)
    {
        Js::GlobalObject* globalObj = value->GetGlobalObject();

        this->m_tagToGlobalObjectMap.AddItem(sctag, globalObj);
        value->ScriptContextLogTag = sctag;
    }

    void InflateMap::AddObject(TTD_PTR_ID objid, Js::RecyclableObject* value)
    {
        this->m_objectMap.AddItem(objid, value);
        this->m_inflatePinSet->AddNew(value);
    }

    void InflateMap::AddInflationFunctionBody(TTD_PTR_ID functionId, Js::FunctionBody* value)
    {
        this->m_functionBodyMap.AddItem(functionId, value);
        //Function bodies are either root (and kept live by our root pin set in the script context/info) or are reachable from it
    }

    void InflateMap::AddEnvironment(TTD_PTR_ID envId, Js::FrameDisplay* value)
    {
        this->m_environmentMap.AddItem(envId, value);
        this->m_environmentPinSet->AddNew(value);
    }

    void InflateMap::AddSlotArray(TTD_PTR_ID slotId, Js::Var* value)
    {
        this->m_slotArrayMap.AddItem(slotId, value);
        this->m_slotArrayPinSet->AddNew(value);
    }

    JsUtil::BaseHashSet<Js::PropertyId, HeapAllocator>& InflateMap::GetPropertyResetSet()
    {
        return this->m_propertyReset;
    }

    Js::Var InflateMap::InflateTTDVar(TTDVar var) const
    {
        if(Js::TaggedNumber::Is(var))
        {
            return static_cast<Js::Var>(var);
        }
        else
        {
            return this->LookupObject(TTD_CONVERT_VAR_TO_PTR_ID(var));
        }
    }

    //////////////////
#if ENABLE_SNAPSHOT_COMPARE
    TTDCompareMap::TTDCompareMap()
        : H1PtrIdWorklist(&HeapAllocator::Instance), H1PtrToH2PtrMap(&HeapAllocator::Instance), SnapObjCmpVTable(nullptr),
        //
        H1TagMap(&HeapAllocator::Instance), 
        H1ValueMap(&HeapAllocator::Instance), H1SlotArrayMap(&HeapAllocator::Instance), H1FunctionScopeInfoMap(&HeapAllocator::Instance),
        H1FunctionTopLevelLoadMap(&HeapAllocator::Instance), H1FunctionTopLevelNewMap(&HeapAllocator::Instance), H1FunctionTopLevelEvalMap(&HeapAllocator::Instance),
        H1FunctionBodyMap(&HeapAllocator::Instance), H1ObjectMap(&HeapAllocator::Instance),
        //
        H2TagMap(&HeapAllocator::Instance), 
        H2ValueMap(&HeapAllocator::Instance), H2SlotArrayMap(&HeapAllocator::Instance), H2FunctionScopeInfoMap(&HeapAllocator::Instance),
        H2FunctionTopLevelLoadMap(&HeapAllocator::Instance), H2FunctionTopLevelNewMap(&HeapAllocator::Instance), H2FunctionTopLevelEvalMap(&HeapAllocator::Instance),
        H2FunctionBodyMap(&HeapAllocator::Instance), H2ObjectMap(&HeapAllocator::Instance)
    {
        this->SnapObjCmpVTable = HeapNewArrayZ(fPtr_AssertSnapEquivAddtlInfo, (int32)NSSnapObjects::SnapObjectType::Limit);

        this->SnapObjCmpVTable[(int32)NSSnapObjects::SnapObjectType::SnapScriptFunctionObject] = &NSSnapObjects::AssertSnapEquiv_SnapScriptFunctionInfo;
        this->SnapObjCmpVTable[(int32)NSSnapObjects::SnapObjectType::SnapExternalFunctionObject] = &NSSnapObjects::AssertSnapEquiv_SnapExternalFunctionInfo;
        this->SnapObjCmpVTable[(int32)NSSnapObjects::SnapObjectType::SnapRuntimeRevokerFunctionObject] = &NSSnapObjects::AssertSnapEquiv_SnapRevokerFunctionInfo;
        this->SnapObjCmpVTable[(int32)NSSnapObjects::SnapObjectType::SnapBoundFunctionObject] = &NSSnapObjects::AssertSnapEquiv_SnapBoundFunctionInfo;
        this->SnapObjCmpVTable[(int32)NSSnapObjects::SnapObjectType::SnapHeapArgumentsObject] = &NSSnapObjects::AssertSnapEquiv_SnapHeapArgumentsInfo;
        this->SnapObjCmpVTable[(int32)NSSnapObjects::SnapObjectType::SnapBoxedValueObject] = &NSSnapObjects::AssertSnapEquiv_SnapBoxedValue;
        this->SnapObjCmpVTable[(int32)NSSnapObjects::SnapObjectType::SnapDateObject] = &NSSnapObjects::AssertSnapEquiv_SnapDate;
        this->SnapObjCmpVTable[(int32)NSSnapObjects::SnapObjectType::SnapRegexObject] = &NSSnapObjects::AssertSnapEquiv_SnapRegexInfo;
        this->SnapObjCmpVTable[(int32)NSSnapObjects::SnapObjectType::SnapArrayObject] = &NSSnapObjects::AssertSnapEquiv_SnapArrayInfo<TTDVar, NSSnapObjects::SnapObjectType::SnapArrayObject>;
        this->SnapObjCmpVTable[(int32)NSSnapObjects::SnapObjectType::SnapNativeIntArrayObject] = &NSSnapObjects::AssertSnapEquiv_SnapArrayInfo<int, NSSnapObjects::SnapObjectType::SnapNativeIntArrayObject>;
        this->SnapObjCmpVTable[(int32)NSSnapObjects::SnapObjectType::SnapNativeFloatArrayObject] = &NSSnapObjects::AssertSnapEquiv_SnapArrayInfo<double, NSSnapObjects::SnapObjectType::SnapNativeFloatArrayObject>;
        this->SnapObjCmpVTable[(int32)NSSnapObjects::SnapObjectType::SnapArrayBufferObject] = &NSSnapObjects::AssertSnapEquiv_SnapArrayBufferInfo;
        this->SnapObjCmpVTable[(int32)NSSnapObjects::SnapObjectType::SnapTypedArrayObject] = &NSSnapObjects::AssertSnapEquiv_SnapTypedArrayInfo;
        this->SnapObjCmpVTable[(int32)NSSnapObjects::SnapObjectType::SnapSetObject] = &NSSnapObjects::AssertSnapEquiv_SnapSetInfo;
        this->SnapObjCmpVTable[(int32)NSSnapObjects::SnapObjectType::SnapMapObject] = &NSSnapObjects::AssertSnapEquiv_SnapMapInfo;
        this->SnapObjCmpVTable[(int32)NSSnapObjects::SnapObjectType::SnapProxyObject] = &NSSnapObjects::AssertSnapEquiv_SnapProxyInfo;
        this->SnapObjCmpVTable[(int32)NSSnapObjects::SnapObjectType::SnapPromiseObject] = &NSSnapObjects::AssertSnapEquiv_SnapPromiseInfo;
        this->SnapObjCmpVTable[(int32)NSSnapObjects::SnapObjectType::SnapPromiseResolveOrRejectFunctionObject] = &NSSnapObjects::AssertSnapEquiv_SnapPromiseResolveOrRejectFunctionInfo;
        this->SnapObjCmpVTable[(int32)NSSnapObjects::SnapObjectType::SnapPromiseReactionTaskFunctionObject] = &NSSnapObjects::AssertSnapEquiv_SnapPromiseReactionTaskFunctionInfo;
    }

    TTDCompareMap::~TTDCompareMap()
    {
        HeapDeleteArray((int32)NSSnapObjects::SnapObjectType::Limit, this->SnapObjCmpVTable);
    }

    void TTDCompareMap::CheckConsistentAndAddPtrIdMapping(TTD_PTR_ID h1PtrId, TTD_PTR_ID h2PtrId)
    {
        if(h1PtrId == TTD_INVALID_PTR_ID || h2PtrId == TTD_INVALID_PTR_ID)
        {
            TTD_DIAGNOSTIC_ASSERT(h1PtrId == TTD_INVALID_PTR_ID && h2PtrId == TTD_INVALID_PTR_ID);
        }
        else if(this->H1PtrToH2PtrMap.ContainsKey(h1PtrId))
        {
            TTD_DIAGNOSTIC_ASSERT(this->H1PtrToH2PtrMap.Lookup(h1PtrId, TTD_INVALID_PTR_ID) == h2PtrId);
        }
        else if(this->H1ValueMap.ContainsKey(h1PtrId))
        {
            TTD_DIAGNOSTIC_ASSERT(this->H2ValueMap.ContainsKey(h2PtrId));

            const NSSnapValues::SnapPrimitiveValue* v1 = this->H1ValueMap.Lookup(h1PtrId, nullptr);
            const NSSnapValues::SnapPrimitiveValue* v2 = this->H2ValueMap.Lookup(h2PtrId, nullptr);
            NSSnapValues::AssertSnapEquiv(v1, v2, *this);
        }
        else
        {
            this->H1PtrIdWorklist.Enqueue(h1PtrId);
            this->H1PtrToH2PtrMap.AddNew(h1PtrId, h2PtrId);
        }
    }

    void TTDCompareMap::CheckConsistentAndAddPtrIdMapping_NoEnqueue(TTD_PTR_ID h1PtrId, TTD_PTR_ID h2PtrId)
    {
        if(h1PtrId == TTD_INVALID_PTR_ID || h2PtrId == TTD_INVALID_PTR_ID)
        {
            TTD_DIAGNOSTIC_ASSERT(h1PtrId == TTD_INVALID_PTR_ID && h2PtrId == TTD_INVALID_PTR_ID);
        }
        else if(this->H1PtrToH2PtrMap.ContainsKey(h1PtrId))
        {
            TTD_DIAGNOSTIC_ASSERT(this->H1PtrToH2PtrMap.Lookup(h1PtrId, TTD_INVALID_PTR_ID) == h2PtrId);
        }
        else
        {
            this->H1PtrToH2PtrMap.AddNew(h1PtrId, h2PtrId);
        }
    }

    void TTDCompareMap::GetNextCompareInfo(TTDCompareTag* tag, TTD_PTR_ID* h1PtrId, TTD_PTR_ID* h2PtrId)
    {
        if(this->H1PtrIdWorklist.Empty())
        {
            *tag = TTDCompareTag::Done;
            *h1PtrId = TTD_INVALID_PTR_ID;
            *h2PtrId = TTD_INVALID_PTR_ID;
        }
        else
        {
            *h1PtrId = this->H1PtrIdWorklist.Dequeue();
            *h2PtrId = this->H1PtrToH2PtrMap.Lookup(*h1PtrId, TTD_INVALID_PTR_ID);
            AssertMsg(*h2PtrId != TTD_INVALID_PTR_ID, "Id not mapped!!!");

            if(this->H1SlotArrayMap.ContainsKey(*h1PtrId))
            {
                TTD_DIAGNOSTIC_ASSERT(this->H2SlotArrayMap.ContainsKey(*h2PtrId));
                *tag = TTDCompareTag::SlotArray;
            }
            else if(this->H1FunctionScopeInfoMap.ContainsKey(*h1PtrId))
            {
                TTD_DIAGNOSTIC_ASSERT(this->H2FunctionScopeInfoMap.ContainsKey(*h2PtrId));
                *tag = TTDCompareTag::FunctionScopeInfo;
            }
            else if(this->H1FunctionTopLevelLoadMap.ContainsKey(*h1PtrId))
            {
                TTD_DIAGNOSTIC_ASSERT(this->H2FunctionTopLevelLoadMap.ContainsKey(*h2PtrId));
                *tag = TTDCompareTag::TopLevelLoadFunction;
            }
            else if(this->H1FunctionTopLevelNewMap.ContainsKey(*h1PtrId))
            {
                TTD_DIAGNOSTIC_ASSERT(this->H2FunctionTopLevelNewMap.ContainsKey(*h2PtrId));
                *tag = TTDCompareTag::TopLevelNewFunction;
            }
            else if(this->H1FunctionTopLevelEvalMap.ContainsKey(*h1PtrId))
            {
                TTD_DIAGNOSTIC_ASSERT(this->H2FunctionTopLevelEvalMap.ContainsKey(*h2PtrId));
                *tag = TTDCompareTag::TopLevelEvalFunction;
            }
            else if(this->H1FunctionBodyMap.ContainsKey(*h1PtrId))
            {
                TTD_DIAGNOSTIC_ASSERT(this->H2FunctionBodyMap.ContainsKey(*h2PtrId));
                *tag = TTDCompareTag::FunctionBody;
            }
            else if(this->H1ObjectMap.ContainsKey(*h1PtrId))
            {
                TTD_DIAGNOSTIC_ASSERT(this->H2ObjectMap.ContainsKey(*h2PtrId));
                *tag = TTDCompareTag::SnapObject;
            }
            else
            {
                AssertMsg(!this->H1ValueMap.ContainsKey(*h1PtrId), "Should be comparing by value!!!");
                AssertMsg(false, "Id not found in any of the maps!!!");
                *tag = TTDCompareTag::Done;
            }
        }
    }

    void TTDCompareMap::GetCompareValues(TTDCompareTag compareTag, TTD_PTR_ID h1PtrId, const NSSnapValues::SlotArrayInfo** val1, TTD_PTR_ID h2PtrId, const NSSnapValues::SlotArrayInfo** val2)
    {
        AssertMsg(compareTag == TTDCompareTag::SlotArray, "Should be a type");
        *val1 = this->H1SlotArrayMap.Lookup(h1PtrId, nullptr);
        *val2 = this->H2SlotArrayMap.Lookup(h2PtrId, nullptr);
    }

    void TTDCompareMap::GetCompareValues(TTDCompareTag compareTag, TTD_PTR_ID h1PtrId, const NSSnapValues::ScriptFunctionScopeInfo** val1, TTD_PTR_ID h2PtrId, const NSSnapValues::ScriptFunctionScopeInfo** val2)
    {
        AssertMsg(compareTag == TTDCompareTag::FunctionScopeInfo, "Should be a type");
        *val1 = this->H1FunctionScopeInfoMap.Lookup(h1PtrId, nullptr);
        *val2 = this->H2FunctionScopeInfoMap.Lookup(h2PtrId, nullptr);
    }

    void TTDCompareMap::GetCompareValues(TTDCompareTag compareTag, TTD_PTR_ID h1PtrId, const NSSnapValues::TopLevelScriptLoadFunctionBodyResolveInfo** val1, TTD_PTR_ID h2PtrId, const NSSnapValues::TopLevelScriptLoadFunctionBodyResolveInfo** val2)
    {
        AssertMsg(compareTag == TTDCompareTag::TopLevelLoadFunction, "Should be a type");
        *val1 = this->H1FunctionTopLevelLoadMap.Lookup(h1PtrId, nullptr);
        *val2 = this->H2FunctionTopLevelLoadMap.Lookup(h2PtrId, nullptr);
    }

    void TTDCompareMap::GetCompareValues(TTDCompareTag compareTag, TTD_PTR_ID h1PtrId, const NSSnapValues::TopLevelNewFunctionBodyResolveInfo** val1, TTD_PTR_ID h2PtrId, const NSSnapValues::TopLevelNewFunctionBodyResolveInfo** val2)
    {
        AssertMsg(compareTag == TTDCompareTag::TopLevelNewFunction, "Should be a type");
        *val1 = this->H1FunctionTopLevelNewMap.Lookup(h1PtrId, nullptr);
        *val2 = this->H2FunctionTopLevelNewMap.Lookup(h2PtrId, nullptr);
    }

    void TTDCompareMap::GetCompareValues(TTDCompareTag compareTag, TTD_PTR_ID h1PtrId, const NSSnapValues::TopLevelEvalFunctionBodyResolveInfo** val1, TTD_PTR_ID h2PtrId, const NSSnapValues::TopLevelEvalFunctionBodyResolveInfo** val2)
    {
        AssertMsg(compareTag == TTDCompareTag::TopLevelEvalFunction, "Should be a type");
        *val1 = this->H1FunctionTopLevelEvalMap.Lookup(h1PtrId, nullptr);
        *val2 = this->H2FunctionTopLevelEvalMap.Lookup(h2PtrId, nullptr);
    }

    void TTDCompareMap::GetCompareValues(TTDCompareTag compareTag, TTD_PTR_ID h1PtrId, const NSSnapValues::FunctionBodyResolveInfo** val1, TTD_PTR_ID h2PtrId, const NSSnapValues::FunctionBodyResolveInfo** val2)
    {
        AssertMsg(compareTag == TTDCompareTag::FunctionBody, "Should be a type");
        *val1 = this->H1FunctionBodyMap.Lookup(h1PtrId, nullptr);
        *val2 = this->H2FunctionBodyMap.Lookup(h2PtrId, nullptr);
    }

    void TTDCompareMap::GetCompareValues(TTDCompareTag compareTag, TTD_PTR_ID h1PtrId, const NSSnapObjects::SnapObject** val1, TTD_PTR_ID h2PtrId, const NSSnapObjects::SnapObject** val2)
    {
        AssertMsg(compareTag == TTDCompareTag::SnapObject, "Should be a type");
        *val1 = this->H1ObjectMap.Lookup(h1PtrId, nullptr);
        *val2 = this->H2ObjectMap.Lookup(h2PtrId, nullptr);
    }
#endif
}

#endif