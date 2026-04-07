#pragma once

// === C++ includes ===
#include <cstdint>
#include <cstddef>
#include <bitset>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <algorithm>
#include <concepts>
#include <type_traits>
#include <stdexcept>
#include <typeindex>

// === Core includes ===
#include <Time/IClock.h>

// === ECS includes ===
#include "ComponentIDHelper.h"

namespace Cue::ECS
{
using Entity = uint32_t;
using CompID = size_t;
using Archetype = std::bitset<256>;

// コンポーネントだと判別するためのタグ
struct IComponentTag
{
    virtual ~IComponentTag() = default; // 仮想デストラクタを定義

    // 初期化関数
    virtual void Initialize() {}

    // アクティブ状態
    bool IsActive() const noexcept { return m_Active; }
    void SetActive(bool active) noexcept { m_Active = active; }

  private:
    bool m_Active = true; // アクティブ状態
};

// コンポーネントが複数持てるか(デフォルトは持てない)
template <typename T>
struct IsMultiComponent : std::false_type
{
};

// コンポーネント型のみ許可する
template <typename T>
concept ComponentType = std::derived_from<T, IComponentTag>;

// ECS用ヘルパー
template <typename C>
concept HasInitialize = requires(C &c) { c.Initialize(); };

struct IIComponentEventListener
{
    // ECS側から呼ばれる
    virtual void OnComponentAdded(Entity e, CompID compType) = 0;
    virtual void OnComponentCopied(Entity src, Entity dst, CompID compType) = 0;
    virtual void OnComponentRemoved(Entity e, CompID compType) = 0;
    virtual void OnComponentRemovedInstance(Entity e, CompID compType, void *rawVec, size_t idx) = 0;
    // Prefabからの復元時に呼ばれる
    virtual void OnComponentRestoredFromPrefab(Entity, CompID) {}
};

struct IEntityEventListener
{
    virtual ~IEntityEventListener() = default;

    /// e が新しく生成された直後に呼ばれる
    virtual void OnEntityCreated(Entity e) = 0;

    /// e のすべてのコンポーネントがクリアされ、RecycleQueue に入った直後に呼ばれる
    virtual void OnEntityDestroyed(Entity e) = 0;
};

class ECSManager
{
    friend class IPrefab;

  public:
    /*---------------------------------------------------------------------
            エンティティ管理
        ---------------------------------------------------------------------*/
    ECSManager() = default;
    explicit ECSManager(Core::Time::IClock &a_clock) noexcept
        : m_Clock(&a_clock)
    {
    }
    ~ECSManager() = default;

    void SetClock(Core::Time::IClock &a_clock) noexcept
    {
        m_Clock = &a_clock;
    }

    // 公開API
    [[nodiscard]]
    bool IsEntityActive(Entity e) const
    {
        return e < m_EntityToActive.size() && m_EntityToActive[e];
    }

    void SetEntityActive(Entity e, bool f)
    {
        if (e < m_EntityToActive.size())
        {
            m_EntityToActive[e] = f;
        }
    }

    /*-------------------- Entity create / recycle ----------------------*/
    [[nodiscard]]
    inline const Entity GenerateEntity()
    {
        Entity entity = m_RecycleEntities.empty()
                            ? ++m_NextEntityID
                            : m_RecycleEntities.back();
        if (!m_RecycleEntities.empty())
        {
            m_RecycleEntities.pop_back();
        }

        // アクティブ化
        if (m_EntityToActive.size() <= entity)
        {
            m_EntityToActive.resize(entity + 1, false);
        }
        m_EntityToActive[entity] = true;

        // Archetype 配列も拡張
        if (m_EntityToArchetype.size() <= entity)
        {
            m_EntityToArchetype.resize(entity + 1);
        }

        // ① 空のアーキタイプ（全ビットfalse）にも必ず登録しておく
        {
            Archetype emptyArch; // 全ビットfalse
            m_ArchToEntities[emptyArch].Add(entity);
        }

        // ② エンティティ生成イベントを通知（更新中なら遅延）
        auto notify = [this, entity]()
        {
            for (auto &wp : m_EntityListeners)
            {
                if (auto sp = wp.lock())
                {
                    sp->OnEntityCreated(entity);
                }
            }
        };
        notify();

        if (m_IsUpdating)
        {
            // Staging に積むだけ（本体に反映しない）
            m_StagingEntities.push_back(entity);
            m_StagingEntityActive.push_back(true);
            m_StagingEntityArchetypes.push_back(Archetype{});
        }
        else
        {
            // 通常通り即時反映
            if (m_EntityToActive.size() <= entity)
            {
                m_EntityToActive.resize(entity + 1, false);
            }
            m_EntityToActive[entity] = true;

            if (m_EntityToArchetype.size() <= entity)
            {
                m_EntityToArchetype.resize(entity + 1);
            }
            m_EntityToArchetype[entity] = Archetype{};

            // m_ArchToEntities[Archetype{}].Add(entity);
        }

        return entity;
    }

    /*-------------------- Clear all components ------------------------*/
    inline void ClearEntity(const Entity &e)
    {
        if (e >= m_EntityToArchetype.size())
        {
            return;
        }
        Archetype old = m_EntityToArchetype[e];

        // ① 削除対象の CompID を collect
        std::vector<CompID> toRemove;
        for (CompID id = 0; id < old.size(); ++id)
        {
            if (old.test(id))
            {
                toRemove.push_back(id);
            }
        }

        // ② 優先度→CompID でソート
        std::sort(toRemove.begin(), toRemove.end(),
                  [&](CompID a, CompID b)
                  {
                      int pa = m_DeletePriority.count(a) ? m_DeletePriority[a] : 0;
                      int pb = m_DeletePriority.count(b) ? m_DeletePriority[b] : 0;
                      if (pa != pb)
                      {
                          return pa < pb;
                      }
                      return a < b;
                  });

        // ③ ソート後の順で通知＆削除
        for (CompID id : toRemove)
        {
            auto *pool = m_TypeToComponents[id].get();
            size_t cnt = pool->GetComponentCount(e);

            for (auto &up : m_Systems)
            {
                up->FinalizeEntity(e);
            }

            if (pool->IsMultiComponentTrait(id))
            {
                void *raw = pool->GetRawComponent(e);
                for (size_t i = 0; i < cnt; ++i)
                {
                    NotifyComponentRemovedInstance(e, id, raw, i);
                }
            }
            else
            {
                NotifyComponentRemoved(e, id);
            }
            pool->Cleanup(e); // クリーンアップ呼び出し
            pool->RemoveComponent(e);
        }

        // バケット・アーキタイプクリア
        m_ArchToEntities[old].Remove(e);
        m_EntityToArchetype[e].reset();
    }

    // ――――――――――――――――――
    //  他のシステムやメインループから呼んでよい、
    //  更新中も安全に追加できる汎用 Defer
    // ――――――――――――――――――
    void Defer(std::function<void()> cmd)
    {
        m_DeferredCommands.push_back(std::move(cmd));
    }

    /*-------------------- Disable entity ------------------------------*/
    inline void RemoveEntity(const Entity &e)
    {
        if (m_IsUpdating)
        {
            // システム更新中なら遅延キューに積む
            Defer([this, e]
                  { RemoveEntityImpl(e); });
        }
        else
        {
            // 通常フレームなら即時実行
            RemoveEntityImpl(e);
        }
    }

    /*-------------------- Copy whole entity ---------------------------*/
    [[nodiscard]]
    Entity CopyEntity(const Entity &src)
    {
        // ① 元のアーキタイプを取得
        const Archetype arch = GetArchetype(src);

        // ② 生成（ステージングなら GenerateEntity もステージングに入るよう修正済み）
        Entity dst = GenerateEntity();

        // ── コピーする CompID を集めて優先度順にソート ──
        std::vector<CompID> toCopy;
        for (CompID id = 0; id < arch.size(); ++id)
        {
            if (arch.test(id))
            {
                toCopy.push_back(id);
            }
        }

        std::sort(toCopy.begin(), toCopy.end(),
                  [&](CompID a, CompID b)
                  {
                      int pa = m_CopyPriority.count(a) ? m_CopyPriority[a] : 0;
                      int pb = m_CopyPriority.count(b) ? m_CopyPriority[b] : 0;
                      if (pa != pb)
                      {
                          return pa < pb;
                      }
                      return a < b;
                  });

        // ③ 優先度順にしてからコピー＆通知
        for (CompID id : toCopy)
        {
            auto *pool = m_TypeToComponents[id].get();
            if (m_IsUpdating)
            {
                pool->CopyComponentStaging(src, dst);
                size_t idx = StagingIndexForEntity(dst);
                m_StagingEntityArchetypes[idx].set(id);
            }
            else
            {
                // 通常フレーム：即時コピー
                pool->CopyComponent(src, dst);

                // → ここで本番バッファにもビットを立てる
                if (m_EntityToArchetype.size() <= dst)
                {
                    m_EntityToArchetype.resize(dst + 1);
                }
                /* Archetype& archDst = m_EntityToArchetype[dst];
                 if (!archDst.test(id))
                 {
                     m_ArchToEntities[archDst].Remove(dst);
                     archDst.set(id);
                     m_ArchToEntities[archDst].Add(dst);
                 }*/
                m_EntityToArchetype[dst].set(id);
            }
            NotifyComponentCopied(src, dst, id);
        }

        // ④ アーキタイプの反映
        if (m_IsUpdating)
        {
            for (auto &sys : m_Systems)
            {
                sys->AwakeEntity(dst);
            }
        }
        else
        {
            // 即時反映
            /*if (m_EntityToArchetype.size() <= dst)
                m_EntityToArchetype.resize(dst + 1);
            m_EntityToArchetype[dst] = arch;
            m_ArchToEntities[arch].Add(dst);*/
            Archetype oldArch = Archetype{};
            Archetype &newArch = m_EntityToArchetype[dst];
            if (oldArch != newArch)
            {
                m_ArchToEntities[oldArch].Remove(dst);
                m_ArchToEntities[newArch].Add(dst);
            }
        }

        return dst;
    }

    //-------------------- Copy into existing entity --------------------
    void CopyEntity(const Entity &src, const Entity &dst)
    {
        // ① ソースのアーキタイプを取得
        const Archetype arch = GetArchetype(src);

        Archetype oldArch = m_IsUpdating ? Archetype{} : GetArchetype(dst);

        // ── コピーする CompID を集めて優先度順にソート ──
        std::vector<CompID> toCopy;
        for (CompID id = 0; id < arch.size(); ++id)
        {
            if (arch.test(id))
            {
                toCopy.push_back(id);
            }
        }

        std::sort(toCopy.begin(), toCopy.end(),
                  [&](CompID a, CompID b)
                  {
                      int pa = m_CopyPriority.count(a) ? m_CopyPriority[a] : 0;
                      int pb = m_CopyPriority.count(b) ? m_CopyPriority[b] : 0;
                      if (pa != pb)
                      {
                          return pa < pb;
                      }
                      return a < b;
                  });

        // ③ 優先度順にしてからコピー＆通知
        for (CompID id : toCopy)
        {
            auto *pool = m_TypeToComponents[id].get();
            if (m_IsUpdating)
            {
                pool->CopyComponentStaging(src, dst);
                size_t idx = StagingIndexForEntity(dst);
                m_StagingEntityArchetypes[idx].set(id);
            }
            else
            {
                // 通常フレーム：即時コピー
                pool->CopyComponent(src, dst);

                // → ここで本番バッファにもビットを立てる
                if (m_EntityToArchetype.size() <= dst)
                {
                    m_EntityToArchetype.resize(dst + 1);
                }
                m_EntityToArchetype[dst].set(id);
            }
            NotifyComponentCopied(src, dst, id);
        }

        // ④ アーキタイプの反映
        if (m_IsUpdating)
        {
            for (auto &sys : m_Systems)
            {
                sys->AwakeEntity(dst);
            }
        }
        else
        {
            // 即時反映
            /*if (m_EntityToArchetype.size() <= dst)
                m_EntityToArchetype.resize(dst + 1);
            m_EntityToArchetype[dst] = arch;
            m_ArchToEntities[arch].Add(dst);*/
            Archetype &newArch = m_EntityToArchetype[dst];
            if (oldArch != newArch)
            {
                m_ArchToEntities[oldArch].Remove(dst);
                m_ArchToEntities[newArch].Add(dst);
            }
        }
    }

    /// Entity e のステージングバッファ内インデックスを返す。
    /// なければ追加して新しいインデックスを返す。
    size_t StagingIndexForEntity(Entity e)
    {
        // 1) 既存エントリがあればその位置を返す
        auto it = std::find(m_StagingEntities.begin(), m_StagingEntities.end(), e);
        if (it != m_StagingEntities.end())
        {
            return static_cast<size_t>(std::distance(m_StagingEntities.begin(), it));
        }

        // 2) なければバッファ末尾に追加
        size_t newIndex = m_StagingEntities.size();
        m_StagingEntities.push_back(e);

        // デフォルト active=true, archetype は空ビットセットで追加
        m_StagingEntityActive.push_back(true);
        m_StagingEntityArchetypes.push_back(Archetype{});

        return newIndex;
    }

    /*-------------------- Copy selected components --------------------*/
    void CopyComponents(Entity src, Entity dst, bool overwrite = true)
    {
        const Archetype &archSrc = GetArchetype(src);

        // コピー先アーキタイプへの参照取得（ステージング中かどうかで使い分け）
        Archetype *pArchDst;
        Archetype oldArch;
        if (m_IsUpdating)
        {
            // dst がステージングリストにあればそのインデックスを取得、
            // なければ追加してからインデックスを取得
            size_t idx = StagingIndexForEntity(dst);
            pArchDst = &m_StagingEntityArchetypes[idx];
        }
        else
        {
            if (m_EntityToArchetype.size() <= dst)
            {
                m_EntityToArchetype.resize(dst + 1);
            }
            pArchDst = &m_EntityToArchetype[dst];
        }
        oldArch = *pArchDst;

        // ソースのビットセットを走査
        for (CompID id = 0; id < archSrc.size(); ++id)
        {
            if (!archSrc.test(id))
            {
                continue;
            }
            if (!overwrite && pArchDst->test(id))
            {
                continue;
            }

            auto *pool = m_TypeToComponents[id].get();
            if (m_IsUpdating)
            {
                // フレーム中はステージングコピー
                pool->CopyComponentStaging(src, dst);
            }
            else
            {
                // フレーム外は即時コピー
                pool->CopyComponent(src, dst);
                NotifyComponentCopied(src, dst, id);
            }

            // アーキタイプのビットをセット
            pArchDst->set(id);
        }

        // 即時フレーム外ならバケット（EntityContainer）も更新
        if (!m_IsUpdating && *pArchDst != oldArch)
        {
            m_ArchToEntities[oldArch].Remove(dst);
            m_ArchToEntities[*pArchDst].Add(dst);
        }
    }

    /*-------------------- Add component -------------------------------*/
    template <ComponentType T>
    T *AddComponent(const Entity &entity)
    {
        CompID type = ComponentPool<T>::GetID();

        // プール取得 or 作成
        auto [it, _] = m_TypeToComponents.try_emplace(
            type,
            std::make_shared<ComponentPool<T>>(4096));
        auto pool = std::static_pointer_cast<ComponentPool<T>>(it->second);

        // 更新中ならステージングに追加して即時参照可能にする
        if (m_IsUpdating)
        {
            T *comp = pool->AddComponentStaging(entity); // ← 後述の新関数で staging に追加

            // Archetype は staging にも保持しておく（後で FlushStagingEntities で反映）
            if (std::find(m_StagingEntities.begin(), m_StagingEntities.end(), entity) == m_StagingEntities.end())
            {
                // Entity がまだ Staging に登録されていなければ追加
                m_StagingEntities.push_back(entity);
                m_StagingEntityActive.push_back(true);
                m_StagingEntityArchetypes.push_back(Archetype{});
            }

            // Archetype を更新（set type ビット）
            for (size_t i = 0; i < m_StagingEntities.size(); ++i)
            {
                if (m_StagingEntities[i] == entity)
                {
                    m_StagingEntityArchetypes[i].set(type);
                    break;
                }
            }

            if constexpr (HasInitialize<T>)
            {
                comp->Initialize();
            }
            // comp->SetOwner(entity);
            // comp->SetECSManager(this);
            NotifyComponentAdded(entity, type);

            return comp; // 即時取得可能
        }

        // 通常フロー（即時反映）
        T *comp = pool->AddComponent(entity);
        // comp->SetOwner(entity);
        // comp->SetECSManager(this);

        if (m_EntityToArchetype.size() <= entity)
        {
            m_EntityToArchetype.resize(entity + 1, Archetype{});
        }
        Archetype &arch = m_EntityToArchetype[entity];
        if (!arch.test(type))
        {
            m_ArchToEntities[arch].Remove(entity);
            arch.set(type);
            m_ArchToEntities[arch].Add(entity);
        }

        if constexpr (HasInitialize<T>)
        {
            comp->Initialize();
        }
        NotifyComponentAdded(entity, type);

        return comp;
    }

    /// Prefab復元時だけ使う、イベントを起こさずコンポーネントを追加
    template <ComponentType T>
    void PrefabAddComponent(Entity e, T const &comp)
    {
        CompID type = ComponentPool<T>::GetID();

        // プール取得または生成
        auto [it, _] = m_TypeToComponents.try_emplace(
            type,
            std::make_shared<ComponentPool<T>>(4096));
        auto pool = std::static_pointer_cast<ComponentPool<T>>(it->second);

        if (m_IsUpdating)
        {
            // ▼ ステージングに即時追加
            pool->AddPrefabComponentStaging(e, comp);
            // T* pComp = pool->GetComponent(e);
            // pComp->SetOwner(e);
            // pComp->SetECSManager(this);

            // ▼ Archetype も staging に反映
            auto itE = std::find(m_StagingEntities.begin(), m_StagingEntities.end(), e);
            if (itE != m_StagingEntities.end())
            {
                size_t i = std::distance(m_StagingEntities.begin(), itE);
                m_StagingEntityArchetypes[i].set(type);
            }
            else
            {
                m_StagingEntities.push_back(e);
                m_StagingEntityActive.push_back(true);
                Archetype arch{};
                arch.set(type);
                m_StagingEntityArchetypes.push_back(arch);
            }

            return; // 遅延不要
        }

        // ▼ 通常の即時追加（更新中でない場合）
        pool->AddPrefabComponent(e, comp);
        // T* pComp = pool->GetComponent(e);
        // pComp->SetOwner(e);
        // pComp->SetECSManager(this);

        if (m_EntityToArchetype.size() <= e)
        {
            m_EntityToArchetype.resize(e + 1, Archetype{});
        }
        Archetype &arch = m_EntityToArchetype[e];

        if (!arch.test(type))
        {
            m_ArchToEntities[arch].Remove(e);
            arch.set(type);
            m_ArchToEntities[arch].Add(e);
        }

        NotifyComponentRestoredFromPrefab(e, ComponentPool<T>::GetID());
    }

    /*-------------------- Get component -------------------------------*/
    template <ComponentType T>
    T *GetComponent(const Entity &entity)
    {
        CompID type = ComponentPool<T>::GetID();
        bool has = false;

        // ステージング中はステージングとメイン両方をチェック
        if (m_IsUpdating)
        {
            // メイン側チェック
            if (entity < m_EntityToArchetype.size() && m_EntityToArchetype[entity].test(type))
            {
                has = true;
            }
            else
            {
                // ステージングバッファ内のアーキタイプをチェック
                auto it = std::find(m_StagingEntities.begin(),
                                    m_StagingEntities.end(), entity);
                if (it != m_StagingEntities.end())
                {
                    size_t idx = it - m_StagingEntities.begin();
                    has = m_StagingEntityArchetypes[idx].test(type);
                }
            }
        }
        else
        {
            // 通常フレームならメインのみチェック
            has = (entity < m_EntityToArchetype.size() &&
                   m_EntityToArchetype[entity].test(type));
        }

        if (!has)
        {
            return nullptr;
        }

        // プールから取得し、ステージング込みの GetComponent を呼ぶ
        auto pool = static_cast<ComponentPool<T> *>(GetRawComponentPool(type));
        return pool->GetComponent(entity);
    }

    /*-------------------- Get all (multi) -----------------------------*/
    template <ComponentType T>
    std::vector<T> *GetAllComponents(const Entity &entity)
        requires IsMultiComponent<T>::value
    {
        auto it = m_TypeToComponents.find(ComponentPool<T>::GetID());
        if (it == m_TypeToComponents.end())
        {
            return nullptr;
        }
        auto pool = std::static_pointer_cast<ComponentPool<T>>(it->second);
        return pool->GetAllComponents(entity);
    }

    /*-------------------- Remove component ----------------------------*/
    template <ComponentType T>
    void RemoveComponent(const Entity &entity)
    {
        if (m_IsUpdating)
        {
            Defer([this, entity]()
                  { RemoveComponent<T>(entity); });
            return;
        }

        static_assert(!IsMultiComponent<T>::value, "Use RemoveAllComponents for multi-instance.");
        CompID type = ComponentPool<T>::GetID();
        if (entity >= m_EntityToArchetype.size() || !m_EntityToArchetype[entity].test(type))
        {
            return;
        }
        auto pool = GetComponentPool<T>();
        if (!pool)
        {
            return;
        }

        // ② 対応するシステムに「このコンポーネントを削除する前のFinalize」を呼ぶ
        for (auto &up : m_Systems)
        {
            if (auto sys = dynamic_cast<System<T> *>(up.get()))
            {
                sys->FinalizeEntity(entity);
            }
        }

        NotifyComponentRemoved(entity, type);
        pool->RemoveComponent(entity);

        Archetype &arch = m_EntityToArchetype[entity];
        m_ArchToEntities[arch].Remove(entity);
        arch.reset(type);
        m_ArchToEntities[arch].Add(entity);
    }

    /*-------------------- Remove ALL multi ----------------------------*/
    template <ComponentType T>
    void RemoveAllComponents(const Entity &entity)
        requires IsMultiComponent<T>::value
    {
        if (m_IsUpdating)
        {
            Defer([this, entity]()
                  { RemoveAllComponents<T>(entity); });
            return;
        }

        auto *pool = GetComponentPool<T>();
        if (!pool)
        {
            return;
        }

        // マルチ用 finalize 呼び
        for (auto &up : m_Systems)
        {
            if (auto ms = dynamic_cast<MultiComponentSystem<T> *>(up.get()))
            {
                ms->FinalizeEntity(entity);
            }
        }

        NotifyComponentRemoved(entity, ComponentPool<T>::GetID());
        pool->RemoveAll(entity);

        Archetype &arch = m_EntityToArchetype[entity];
        arch.reset(ComponentPool<T>::GetID());
        m_ArchToEntities[arch].Remove(entity);
        m_ArchToEntities[arch].Add(entity);
    }

    // マルチコンポーネントの単一インスタンスを消す
    template <ComponentType T>
    void RemoveComponentInstance(const Entity &e, size_t index)
        requires IsMultiComponent<T>::value
    {
        if (m_IsUpdating)
        {
            Defer([this, e, index]()
                  { RemoveComponentInstance<T>(e, index); });
            return;
        }

        CompID id = ComponentPool<T>::GetID();
        auto *pool = GetComponentPool<T>();
        void *rawVec = pool ? pool->GetRawComponent(e) : nullptr;
        if (pool)
        {
            auto *vec = static_cast<std::vector<T> *>(rawVec);
            if (vec && index < vec->size())
            {
                NotifyComponentRemovedInstance(e, id, rawVec, index);
            }
        }

        if (pool)
        {
            pool->RemoveInstance(e, index);
        }

        auto &arch = m_EntityToArchetype[e];
        auto *remaining = pool ? pool->GetAllComponents(e) : nullptr;
        if (!remaining || remaining->empty())
        {
            m_ArchToEntities[arch].Remove(e);
            arch.reset(id);
            m_ArchToEntities[arch].Add(e);
        }
    }

    // コンポーネント別に「削除時の優先度」を設定できるように
    template <ComponentType T>
    void SetDeletionPriority(int priority)
    {
        m_DeletePriority[ComponentPool<T>::GetID()] = priority;
    }

    // コピー時の優先度を設定するテンプレート関数
    template <ComponentType T>
    void SetCopyPriority(int priority)
    {
        m_CopyPriority[ComponentPool<T>::GetID()] = priority;
    }

    /*-------------------- Accessors ----------------------------------*/
    inline const Archetype &GetArchetype(Entity e) const
    {
        static Archetype empty{};
        return (e < m_EntityToArchetype.size()) ? m_EntityToArchetype[e] : empty;
    }

    /// コンポーネントイベントリスナーを登録（shared_ptr で受け取る）
    void AddComponentListener(std::shared_ptr<IIComponentEventListener> listener)
    {
        m_ComponentListeners.emplace_back(listener);
    }

    /// エンティティイベントリスナーを登録（shared_ptr で受け取る）
    void AddEntityListener(std::shared_ptr<IEntityEventListener> listener)
    {
        m_EntityListeners.emplace_back(listener);
    }
    /// (オプション) 全リスナーをクリア
    void ClearComponentListeners()
    {
        m_ComponentListeners.clear();
    }
    void ClearEntityListeners()
    {
        m_EntityListeners.clear();
    }

    // ――――――――――――――――――
    //  システム登録
    // ――――――――――――――――――
    template <typename S, typename... Args>
    S &AddSystem(Args &&...args)
    {
        static_assert(std::is_base_of_v<ISystem, S>, "S must derive from ISystem");
        auto ptr = std::make_unique<S>(std::forward<Args>(args)...);
        S *raw = ptr.get();
        // 所属する ECSManager のポインタを渡す
        raw->OnRegister(this);
        m_Systems.push_back(std::move(ptr));
        return *raw;
    }
    template <typename S>
    S *GetSystem()
    {
        for (auto &up : m_Systems)
        {
            if (auto p = dynamic_cast<S *>(up.get()))
            {
                return p;
            }
        }
        return nullptr;
    }

    // 更新ループを中止するAPI
    void CancelUpdateLoop()
    {
        m_CancelUpdate = true;
    }

    // ① ゲーム開始前に一度だけ
    void InitializeAllSystems()
    {
        m_IsUpdating = true;
        m_CancelUpdate = false; // ← 毎フレーム最初にリセット
        const Math::TimeSpan t0Total = capture_now_();

        sort_systems_by_priority_();
        for (auto &sys : m_Systems)
        {
            if (sys->IsEnabled())
            {
                const Math::TimeSpan t0 = capture_now_();
                sys->Initialize();
                const Math::TimeSpan t1 = capture_now_();
                std::type_index ti(typeid(*sys));
                m_LastSystemInitializeTimeMs[ti] = elapsed_ms_(t0, t1);
            }
        }

        m_IsUpdating = false;
        m_CancelUpdate = false;

        const Math::TimeSpan t1Total = capture_now_();
        m_LastTotalInitializeTimeMs = elapsed_ms_(t0Total, t1Total);
    }

    // ――――――――――――――――――
    //  フレーム毎に呼ぶ：全システムを優先度順に更新
    // ――――――――――――――――――
    void UpdateAllSystems()
    {
        m_IsUpdating = true;
        m_CancelUpdate = false; // ← 毎フレーム最初にリセット

        const Math::TimeSpan t0Total = capture_now_();

        // 追加直後の Entity を初期化システムに通す
        for (Entity e : m_NewEntitiesLastFrame)
        {
            for (auto &sys : m_Systems)
            {
                sys->InitializeEntity(e);
            }
        }
        m_NewEntitiesLastFrame.clear();

        // システム更新
        sort_systems_by_priority_();
        for (auto &sys : m_Systems)
        {
            if (sys->IsEnabled())
            {
                if (m_CancelUpdate)
                {
                    break; // 中止判定
                }
                const Math::TimeSpan t0 = capture_now_();
                sys->Update();
                const Math::TimeSpan t1 = capture_now_();
                std::type_index ti(typeid(*sys));
                m_LastSystemUpdateTimeMs[ti] = elapsed_ms_(t0, t1);
            }
        }

        m_IsUpdating = false;
        m_CancelUpdate = false;
        FlushStagingEntities();
        FlushStagingComponents();
        FlushDeferred();

        const Math::TimeSpan t1Total = capture_now_();
        m_LastTotalUpdateTimeMs = elapsed_ms_(t0Total, t1Total);
    }

    // ③ ゲーム終了後に一度だけ
    void FinalizeAllSystems()
    {
        m_IsUpdating = true;
        m_CancelUpdate = false; // ← 毎フレーム最初にリセット
        const Math::TimeSpan t0Total = capture_now_();

        sort_systems_by_priority_();
        for (auto &sys : m_Systems)
        {
            if (sys->IsEnabled())
            {
                const Math::TimeSpan t0 = capture_now_();
                sys->Finalize();
                const Math::TimeSpan t1 = capture_now_();

                std::type_index ti(typeid(*sys));
                m_LastSystemFinalizeTimeMs[ti] = elapsed_ms_(t0, t1);
            }
        }

        m_IsUpdating = false;
        m_CancelUpdate = false;

        const Math::TimeSpan t1Total = capture_now_();
        m_LastTotalFinalizeTimeMs = elapsed_ms_(t0Total, t1Total);
    }

    // ① ゲーム開始前に一度だけ
    void AwakeAllSystems()
    {
        m_IsUpdating = true;
        m_CancelUpdate = false; // ← 毎フレーム最初にリセット
        const Math::TimeSpan t0Total = capture_now_();

        sort_systems_by_priority_();
        for (auto &sys : m_Systems)
        {
            if (sys->IsEnabled())
            {
                const Math::TimeSpan t0 = capture_now_();
                sys->Awake();
                const Math::TimeSpan t1 = capture_now_();
                std::type_index ti(typeid(*sys));
                m_LastSystemAwakeTimeMs[ti] = elapsed_ms_(t0, t1);
            }
        }

        m_IsUpdating = false;
        m_CancelUpdate = false;

        const Math::TimeSpan t1Total = capture_now_();
        m_LastTotalAwakeTimeMs = elapsed_ms_(t0Total, t1Total);
    }

    /// 最後のフレームで更新に要した「全システム分」の時間を取得(ms)
    double GetLastTotalUpdateTimeMs() const
    {
        return m_LastTotalUpdateTimeMs;
    }

    /// 最後のフレームで更新に要した、システム型 S の時間を取得(ms)
    template <typename S>
    double GetLastSystemUpdateTimeMs() const
    {
        std::type_index ti(typeid(S));
        auto it = m_LastSystemUpdateTimeMs.find(ti);
        if (it == m_LastSystemUpdateTimeMs.end())
        {
            return 0.0;
        }
        return it->second;
    }

    // Initialize
    double GetLastTotalInitializeTimeMs() const
    {
        return m_LastTotalInitializeTimeMs;
    }

    template <typename S>
    double GetLastSystemInitializeTimeMs() const
    {
        auto it = m_LastSystemInitializeTimeMs.find(typeid(S));
        return it == m_LastSystemInitializeTimeMs.end() ? 0.0 : it->second;
    }

    // Finalize
    double GetLastTotalFinalizeTimeMs() const
    {
        return m_LastTotalFinalizeTimeMs;
    }

    template <typename S>
    double GetLastSystemFinalizeTimeMs() const
    {
        auto it = m_LastSystemFinalizeTimeMs.find(typeid(S));
        return it == m_LastSystemFinalizeTimeMs.end() ? 0.0 : it->second;
    }

    void FlushStagingEntities()
    {
        for (size_t i = 0; i < m_StagingEntities.size(); ++i)
        {
            Entity e = m_StagingEntities[i];

            // m_EntityToActive を拡張して反映
            if (m_EntityToActive.size() <= e)
            {
                m_EntityToActive.resize(e + 1, false);
            }
            m_EntityToActive[e] = m_StagingEntityActive[i];

            // Archetype を反映
            if (m_EntityToArchetype.size() <= e)
            {
                m_EntityToArchetype.resize(e + 1);
            }
            m_EntityToArchetype[e] = m_StagingEntityArchetypes[i];

            // Archetype バケットに登録
            m_ArchToEntities[m_StagingEntityArchetypes[i]].Add(e);

            // 新規Entityリストに追加
            m_NewEntitiesLastFrame.push_back(e);
        }

        // 一時バッファをクリア
        m_StagingEntities.clear();
        m_StagingEntityActive.clear();
        m_StagingEntityArchetypes.clear();
    }
    void FlushStagingComponents()
    {
        for (auto &[id, pool] : m_TypeToComponents)
        {
            pool->FlushStaging();
        }
    }

    /*---------------------------------------------------------------------
        EntityContainer (archetype bucket)
    ---------------------------------------------------------------------*/
    class EntityContainer
    {
      public:
        void Add(Entity e)
        {
            m_Entities.emplace_back(e);
            if (m_EntityToIndex.size() <= e)
            {
                m_EntityToIndex.resize(e + 1);
            }
            m_EntityToIndex[e] = static_cast<uint32_t>(m_Entities.size() - 1);
        }
        void Remove(Entity e)
        {
            if (e >= m_EntityToIndex.size())
            {
                return;
            }
            uint32_t idx = m_EntityToIndex[e];
            if (idx >= m_Entities.size())
            {
                return;
            }
            uint32_t last = static_cast<uint32_t>(m_Entities.size() - 1);
            Entity back = m_Entities[last];
            if (e != back)
            {
                m_Entities[idx] = back;
                m_EntityToIndex[back] = idx;
            }
            m_Entities.pop_back();
        }
        const std::vector<Entity> &GetEntities() const noexcept { return m_Entities; }

      private:
        std::vector<Entity> m_Entities;
        std::vector<uint32_t> m_EntityToIndex;
    };

    /*---------------------------------------------------------------------
        IComponentPool (interface)
    ---------------------------------------------------------------------*/
    class IComponentPool
    {
      public:
        virtual ~IComponentPool() = default;
        virtual void CopyComponent(Entity src, Entity dst) = 0;
        virtual void RemoveComponent(Entity e) = 0;
        virtual void *GetRawComponent(Entity e) const = 0;
        virtual std::shared_ptr<void> CloneComponent(CompID id, void *ptr) = 0;
        virtual bool IsMultiComponentTrait(CompID id) const = 0;
        virtual size_t GetComponentCount(Entity e) const = 0;
        /// 生ポインタから直接エンティティ dst にクローンして追加
        virtual void CloneRawComponentTo(Entity dst, void *raw) = 0;
        /// 削除前のクリーンアップ呼び出し用
        virtual void Cleanup(Entity) {}
        virtual void FlushStaging() {} // ステージングバッファをフラッシュ
        virtual void CopyComponentStaging(Entity src, Entity dst)
        {
            // デフォルトでは通常コピーにフォールバック
            CopyComponent(src, dst);
        }
    };

    IComponentPool *GetRawComponentPool(CompID id)
    {
        auto it = m_TypeToComponents.find(id);
        return (it != m_TypeToComponents.end()) ? it->second.get() : nullptr;
    }

    /*---------------------------------------------------------------------
        ComponentPool  (vector‑backed)
    ---------------------------------------------------------------------*/
    template <ComponentType T>
    class ComponentPool : public IComponentPool
    {
        using Storage = std::vector<T>; // ★ vector 版
        static constexpr uint32_t kInvalid = ~0u;

      public:
        explicit ComponentPool(size_t reserveEntities = 0) { m_Storage.reserve(reserveEntities); }

        // 登録された型がマルチかどうかを返す
        bool IsMultiComponentTrait(CompID) const override
        {
            return IsMultiComponent<T>::value;
        }

        /*-------------------- add ---------------------*/
        T *AddComponent(Entity e)
        {
            if constexpr (IsMultiComponent<T>::value)
            {
                return &m_Multi[e].emplace_back(); // vector of components per entity
            }
            else
            {
                if (m_EntityToIndex.size() <= e)
                {
                    m_EntityToIndex.resize(e + 1, kInvalid);
                }
                uint32_t idx = m_EntityToIndex[e];
                if (idx == kInvalid)
                {
                    idx = static_cast<uint32_t>(m_Storage.size());
                    m_Storage.emplace_back(); // default construct
                    if (m_IndexToEntity.size() <= idx)
                    {
                        m_IndexToEntity.resize(idx + 1, kInvalid);
                    }
                    m_EntityToIndex[e] = idx;
                    m_IndexToEntity[idx] = e;
                }
                return &m_Storage[idx];
            }
        }

        T *AddComponentStaging(Entity e)
        {
            if constexpr (IsMultiComponent<T>::value)
            {
                auto &vec = m_StagingMulti[e];
                vec.emplace_back();
                T &inst = vec.back();
                // inst.SetOwner(e);
                // inst.SetECSManager(m_pEcs);
                return &inst;
            }
            else
            {
                // 上書きも可能
                T &inst = m_StagingSingle[e];
                // inst.SetOwner(e);
                // inst.SetECSManager(m_pEcs);
                return &inst;
            }
        }

        /*-------------------- get ---------------------*/
        T *GetComponent(Entity e)
        {
            if constexpr (IsMultiComponent<T>::value)
            {
                // ① 一時バッファにあるかチェック
                auto itStaging = m_StagingMulti.find(e);
                if (itStaging != m_StagingMulti.end() && !itStaging->second.empty())
                {
                    return &itStaging->second.front();
                }

                // ② 本番マップから取得
                auto it = m_Multi.find(e);
                return (it != m_Multi.end() && !it->second.empty()) ? &it->second.front() : nullptr;
            }
            else
            {
                // ① 一時バッファにあるかチェック
                auto itStaging = m_StagingSingle.find(e);
                if (itStaging != m_StagingSingle.end())
                {
                    return &itStaging->second;
                }

                // ② 本番ストレージから取得
                if (e >= m_EntityToIndex.size())
                {
                    return nullptr;
                }
                uint32_t idx = m_EntityToIndex[e];
                return (idx != kInvalid) ? &m_Storage[idx] : nullptr;
            }
        }

        /*-------------------- remove ------------------*/
        void RemoveComponent(Entity e) override
        {
            if constexpr (IsMultiComponent<T>::value)
            {
                m_Multi.erase(e);
            }
            else
            {
                if (e >= m_EntityToIndex.size())
                {
                    return;
                }
                uint32_t idx = m_EntityToIndex[e];
                if (idx == kInvalid)
                {
                    return;
                }
                uint32_t last = static_cast<uint32_t>(m_Storage.size() - 1);
                if (idx != last)
                {
                    m_Storage[idx] = std::move(m_Storage[last]);
                    Entity movedEnt = m_IndexToEntity[last];
                    m_IndexToEntity[idx] = movedEnt;
                    m_EntityToIndex[movedEnt] = idx;
                }
                m_Storage.pop_back();
                m_EntityToIndex[e] = kInvalid;
                m_IndexToEntity[last] = kInvalid;
            }
        }

        /*-------------------- multi helpers -----------*/
        std::vector<T> *GetAllComponents(Entity e)
            requires IsMultiComponent<T>::value
        {
            // ① ステージングバッファ中のマルチコンポーネントを優先
            auto itStaging = m_StagingMulti.find(e);
            if (itStaging != m_StagingMulti.end() && !itStaging->second.empty())
            {
                return &itStaging->second;
            }

            // ② なければ本番マップを返す
            auto itMain = m_Multi.find(e);
            return (itMain != m_Multi.end() && !itMain->second.empty())
                       ? &itMain->second
                       : nullptr;
        }
        // マルチコンポーネント用：特定インデックスの要素を消す
        void RemoveInstance(Entity e, size_t index)
            requires IsMultiComponent<T>::value
        {
            auto it = m_Multi.find(e);
            if (it == m_Multi.end())
            {
                return;
            }
            auto &vec = it->second;
            if (index >= vec.size())
            {
                return;
            }
            vec.erase(vec.begin() + index);
            if (vec.empty())
            {
                // すべて消えたらバケットからも削除
                m_Multi.erase(e);
            }
        }
        void RemoveAll(Entity e)
            requires IsMultiComponent<T>::value
        {
            m_Multi.erase(e);
        }

        /*-------------------- copy --------------------*/
        void CopyComponent(Entity src, Entity dst) override
        {
            if constexpr (IsMultiComponent<T>::value)
            {
                auto it = m_Multi.find(src);
                if (it == m_Multi.end() || it->second.empty())
                {
                    return;
                }
                // shallow copy
                auto &vecSrc = it->second;
                auto &vecDst = m_Multi[dst] = vecSrc; // copy vector
                vecDst;
                //           for (auto& inst : vecDst)
                //           {
                ////inst.SetOwner(dst);
                //           }
            }
            else
            {
                uint32_t idxSrc = m_EntityToIndex[src];
                if (idxSrc == kInvalid)
                {
                    return;
                }
                // すでに dst にコンポーネントがある場合
                if (dst < m_EntityToIndex.size() && m_EntityToIndex[dst] != kInvalid)
                {
                    auto &c = m_Storage[m_EntityToIndex[dst]];
                    c = m_Storage[idxSrc];
                }
                else
                {
                    // 新規にコピーする場合
                    m_Storage.emplace_back(m_Storage[idxSrc]);
                    auto &newComp = m_Storage.back();
                    newComp;

                    uint32_t idxDst = static_cast<uint32_t>(m_Storage.size() - 1);
                    if (m_EntityToIndex.size() <= dst)
                    {
                        m_EntityToIndex.resize(dst + 1, kInvalid);
                    }
                    if (m_IndexToEntity.size() <= idxDst)
                    {
                        m_IndexToEntity.resize(idxDst + 1, kInvalid);
                    }
                    m_EntityToIndex[dst] = idxDst;
                    m_IndexToEntity[idxDst] = dst;
                }
                /*T* comp = GetComponent(dst);
                if (comp)
                {
                    comp->SetOwner(dst);
                }*/
            }
        }

        // 更新中のステージング用コピーをオーバーライド
        void CopyComponentStaging(Entity src, Entity dst) override
        {
            if constexpr (IsMultiComponent<T>::value)
            {
                // マルチコンポーネントなら全インスタンスをステージングに追加
                auto it = m_Multi.find(src);
                if (it == m_Multi.end())
                {
                    return;
                }
                for (auto const &inst : it->second)
                {
                    m_StagingMulti[dst].push_back(inst);
                    // m_StagingMulti[dst].back().SetOwner(dst);
                    // m_StagingMulti[dst].back().SetECSManager(m_pEcs);
                }
            }
            else
            {
                // シングルコンポーネントなら最新の値をステージングバッファに上書き
                T *srcComp = GetComponent(src);
                if (!srcComp)
                {
                    return;
                }
                m_StagingSingle[dst] = *srcComp;
                // m_StagingSingle[dst].SetOwner(dst);
                // m_StagingSingle[dst].SetECSManager(m_pEcs);
            }
        }

        void CloneRawComponentTo(Entity dst, void *raw) override
        {
            if constexpr (IsMultiComponent<T>::value)
            {
                // マルチコンポーネントなら vector<T> 全体をコピー
                auto *srcVec = static_cast<std::vector<T> *>(raw);
                m_Multi[dst] = *srcVec;
            }
            else
            {
                // 単一コンポーネントなら AddComponent＋コピー代入
                T *dstComp = AddComponent(dst);
                *dstComp = *static_cast<T *>(raw);
            }
        }

        /// Prefab復元時だけ使う、「生のコピー」を行う
        void PrefabCloneRaw(Entity e, void *rawPtr)
        {
            if constexpr (IsMultiComponent<T>::value)
            {
                // マルチコンポーネントなら vector<T> 全体を直接コピー
                auto *srcVec = static_cast<std::vector<T> *>(rawPtr);
                m_Multi[e] = *srcVec; // コピーコンストラクタを使う
            }
            else
            {
                // 単一コンポーネントなら storage に直接 emplace_back
                // （operator= ではなく、T のコピーコンストラクタで構築される）
                if (m_EntityToIndex.size() <= e)
                {
                    m_EntityToIndex.resize(e + 1, kInvalid);
                }
                uint32_t idx = static_cast<uint32_t>(m_Storage.size());
                m_Storage.emplace_back(*static_cast<T *>(rawPtr));
                if (m_IndexToEntity.size() <= idx)
                {
                    m_IndexToEntity.resize(idx + 1, kInvalid);
                }
                m_EntityToIndex[e] = idx;
                m_IndexToEntity[idx] = e;
            }
        }

        void AddPrefabComponentStaging(Entity e, const T &comp)
        {
            if constexpr (IsMultiComponent<T>::value)
            {
                m_StagingMulti[e].push_back(comp);
                // m_StagingMulti[e].back().SetOwner(e);
                // m_StagingMulti[e].back().SetECSManager(m_pEcs);
            }
            else
            {
                m_StagingSingle[e] = comp;
                // m_StagingSingle[e].SetOwner(e);
                // m_StagingSingle[e].SetECSManager(m_pEcs);
            }
        }

        void AddPrefabComponent(Entity e, const T &comp)
        {
            if constexpr (IsMultiComponent<T>::value)
            {
                m_Multi[e].push_back(comp);
            }
            else
            {
                if (m_EntityToIndex.size() <= e)
                {
                    m_EntityToIndex.resize(e + 1, kInvalid);
                }
                uint32_t &idx = m_EntityToIndex[e];
                if (idx == kInvalid)
                {
                    idx = static_cast<uint32_t>(m_Storage.size());
                    m_Storage.push_back(comp);
                    if (m_IndexToEntity.size() <= idx)
                    {
                        m_IndexToEntity.resize(idx + 1, kInvalid);
                    }
                    m_IndexToEntity[idx] = e;
                }
                else
                {
                    m_Storage[idx] = comp;
                }
            }
        }

        // ─── 削除前のクリーンアップ
        void Cleanup(Entity e) override
        {
            if constexpr (IsMultiComponent<T>::value)
            {
                auto it = m_Multi.find(e);
                if (it != m_Multi.end())
                {
                    for (auto &inst : it->second)
                    {
                        inst.Initialize();
                    }
                }
            }
            else
            {
                T *comp = GetComponent(e);
                if (comp)
                {
                    comp->Initialize();
                }
            }
        }

        /*-------------------- static ID --------------*/
        static CompID GetID()
        {
            // static CompID id = ++ECSManager::m_NextCompTypeID; return id;
            return ComponentID<T>();
        }

        /*-------------------- expose map -------------*/
        auto &Map() { return m_Multi; }
        const auto &Map() const { return m_Multi; }

        // 単一コンポーネントを void* で取得
        void *GetRawComponent(Entity e) const override
        {
            if constexpr (IsMultiComponent<T>::value)
            {
                auto itS = m_StagingMulti.find(e);
                if (itS != m_StagingMulti.end() && !itS->second.empty())
                {
                    return (void *)&itS->second;
                }
                auto itM = m_Multi.find(e);
                return (itM != m_Multi.end() && !itM->second.empty())
                           ? (void *)&itM->second
                           : nullptr;
            }
            else
            {
                auto itS = m_StagingSingle.find(e);
                if (itS != m_StagingSingle.end())
                {
                    return (void *)&itS->second;
                }
                if (e >= m_EntityToIndex.size())
                {
                    return nullptr;
                }
                uint32_t idx = m_EntityToIndex[e];
                return idx != kInvalid ? (void *)&m_Storage[idx] : nullptr;
            }
        }

        // 任意の型を shared_ptr<void> に包んでPrefabにコピー
        std::shared_ptr<void> CloneComponent(CompID, void *ptr) override
        {
            if constexpr (IsMultiComponent<T>::value)
            {
                auto src = static_cast<std::vector<T> *>(ptr);
                return std::make_shared<std::vector<T>>(*src); // Deep copy
            }
            else
            {
                T *src = static_cast<T *>(ptr);
                return std::make_shared<T>(*src); // Deep copy
            }
        }
        size_t GetComponentCount(Entity e) const override
        {
            if constexpr (IsMultiComponent<T>::value)
            {
                auto it = m_Multi.find(e);
                return (it == m_Multi.end()) ? 0u : it->second.size();
            }
            else
            {
                if (e >= m_EntityToIndex.size())
                {
                    return 0u;
                }
                return (m_EntityToIndex[e] != kInvalid) ? 1u : 0u;
            }
        }
        void FlushStaging() override
        {
            if constexpr (IsMultiComponent<T>::value)
            {
                for (auto &[e, stagingVec] : m_StagingMulti)
                {
                    auto &vec = m_Multi[e];
                    vec.insert(vec.end(), stagingVec.begin(), stagingVec.end());
                }
                m_StagingMulti.clear();
            }
            else
            {
                for (auto &[e, comp] : m_StagingSingle)
                {
                    if (m_EntityToIndex.size() <= e)
                    {
                        m_EntityToIndex.resize(e + 1, kInvalid);
                    }
                    uint32_t idx = m_EntityToIndex[e];

                    if (idx == kInvalid)
                    {
                        idx = static_cast<uint32_t>(m_Storage.size());
                        m_Storage.push_back(comp);
                        if (m_IndexToEntity.size() <= idx)
                        {
                            m_IndexToEntity.resize(idx + 1, kInvalid);
                        }
                        m_EntityToIndex[e] = idx;
                        m_IndexToEntity[idx] = e;
                    }
                    else
                    {
                        m_Storage[idx] = comp; // 上書き
                    }
                }
                m_StagingSingle.clear();
            }
        }
        const std::unordered_map<Entity, std::vector<T>> &GetStagingMulti() const
        {
            return m_StagingMulti;
        }

      private:
        Storage m_Storage;                                  // dense
        std::vector<uint32_t> m_EntityToIndex;              // entity -> index
        std::vector<Entity> m_IndexToEntity;                // index  -> entity
        std::unordered_map<Entity, std::vector<T>> m_Multi; // multi‑instance
        std::unordered_map<Entity, T> m_StagingSingle;      // フレーム中に追加された T
        std::unordered_map<Entity, std::vector<T>> m_StagingMulti;
    };

    template <ComponentType T>
    ComponentPool<T> *GetComponentPool()
    {
        auto it = m_TypeToComponents.find(ComponentPool<T>::GetID());
        if (it == m_TypeToComponents.end())
        {
            return nullptr;
        }
        return static_cast<ComponentPool<T> *>(it->second.get());
    }

    template <ComponentType T>
    ComponentPool<T> &EnsurePool()
    {
        CompID id = ComponentPool<T>::GetID();
        auto [it, inserted] = m_TypeToComponents.try_emplace(
            id,
            std::make_shared<ComponentPool<T>>(4096));
        return *static_cast<ComponentPool<T> *>(it->second.get());
    }

    class ISystem
    {
      public:
        ISystem() : m_pEcs(nullptr) {}
        /// 開始時に一度だけ呼ばれる
        virtual void Initialize() {}
        /// 毎フレーム呼ばれる
        virtual void Update() = 0;
        /// 終了時に一度だけ呼ばれる
        virtual void Finalize() {}
        /// 一度だけ呼ばれる
        virtual void Awake() {}
        virtual ~ISystem() = default;
        /// ECSManager に登録されたタイミングで呼び出される
        virtual void OnRegister(ECSManager *ecs)
        {
            m_pEcs = ecs;
        }
        /// フレーム中に遅延で追加されたエンティティ／コンポーネントを受け取って、
        /// そのエンティティだけフェーズの処理を走らせたいときに使う
        virtual void InitializeEntity(Entity) {}
        virtual void FinalizeEntity(Entity) {}
        virtual void AwakeEntity(Entity) {}
        virtual int GetPriority() const { return priority; }
        virtual void SetPriority(int p) { priority = p; }
        virtual bool IsEnabled() const { return enabled; }
        virtual void SetEnabled(bool e) { enabled = e; }

      protected:
        ECSManager *m_pEcs = nullptr; // ECSManager へのポインタ（初期化時に設定される）
        uint32_t priority = 0;        // 優先度
        bool enabled = true;          // 有効フラグ
    };

    /*---------------------------------------------------------------------
        System & MultiSystem  (unchanged logic)
    ---------------------------------------------------------------------*/
    template <ComponentType... T>
    class System : public ISystem
    {
        static constexpr bool kNoMulti = (!IsMultiComponent<T>::value && ...);
        static_assert(kNoMulti, "System<T...> cannot include multi-instance components");
        using UpdateFunc = std::function<void(Entity, T &...)>;
        using InitFunc = std::function<void(Entity, T &...)>;
        using FinFunc = std::function<void(Entity, T &...)>;
        using AwakeFunc = std::function<void(Entity, T &...)>;

      public:
        explicit System(
            UpdateFunc u,
            InitFunc i = {},
            FinFunc f = {},
            AwakeFunc w = {})
            : m_Update(u), m_Init(i), m_Fin(f), m_Awake(w)
        {
            (m_Required.set(ComponentPool<T>::GetID()), ...);
        }
        // 初期化フェーズでエンティティごとの処理
        void Initialize() override
        {
            if (!m_Init)
            {
                return;
            }
            for (auto &[arch, bucket] : m_pEcs->GetArchToEntities())
            {
                if ((arch & m_Required) != m_Required)
                {
                    continue;
                }
                for (Entity e : bucket.GetEntities())
                {
                    if (!m_pEcs->IsEntityActive(e))
                    {
                        continue;
                    }
                    if (!((m_pEcs->GetComponent<T>(e) != nullptr) && ...))
                    {
                        continue;
                    }
                    if (!((m_pEcs->GetComponent<T>(e)->IsActive()) && ...))
                    {
                        continue;
                    }
                    m_Init(e, *m_pEcs->GetComponent<T>(e)...);
                }
            }
        }
        void Update() override
        {
            for (auto &[arch, bucket] : m_pEcs->GetArchToEntities())
            {
                // アーキタイプフィルタ
                if ((arch & m_Required) != m_Required)
                {
                    continue;
                }

                for (Entity e : bucket.GetEntities())
                {
                    // 1) エンティティが非アクティブならスキップ
                    if (!m_pEcs->IsEntityActive(e))
                    {
                        continue;
                    }

                    // 2) 全コンポーネントが存在するかチェック
                    if (!((m_pEcs->GetComponent<T>(e) != nullptr) && ...))
                    {
                        continue;
                    }

                    // 3) 全コンポーネントがアクティブかチェック
                    if (!((m_pEcs->GetComponent<T>(e)->IsActive()) && ...))
                    {
                        continue;
                    }

                    // 4) 問題なければコールバック
                    m_Update(e, *m_pEcs->GetComponent<T>(e)...);
                }
            }
        }
        // 終了フェーズでエンティティごとの処理
        void Finalize() override
        {
            if (!m_Fin)
            {
                return;
            }
            for (auto &[arch, bucket] : m_pEcs->GetArchToEntities())
            {
                if ((arch & m_Required) != m_Required)
                {
                    continue;
                }
                for (Entity e : bucket.GetEntities())
                {
                    if (!m_pEcs->IsEntityActive(e))
                    {
                        continue;
                    }
                    if (!((m_pEcs->GetComponent<T>(e) != nullptr) && ...))
                    {
                        continue;
                    }
                    if (!((m_pEcs->GetComponent<T>(e)->IsActive()) && ...))
                    {
                        continue;
                    }
                    m_Fin(e, *m_pEcs->GetComponent<T>(e)...);
                }
            }
        }
        // Awake フェーズでエンティティごとの処理
        void Awake() override
        {
            if (!m_Awake)
            {
                return;
            }
            for (auto &[arch, bucket] : m_pEcs->GetArchToEntities())
            {
                if ((arch & m_Required) != m_Required)
                {
                    continue;
                }
                for (Entity e : bucket.GetEntities())
                {
                    if (!m_pEcs->IsEntityActive(e))
                    {
                        continue;
                    }
                    if (!((m_pEcs->GetComponent<T>(e) != nullptr) && ...))
                    {
                        continue;
                    }
                    if (!((m_pEcs->GetComponent<T>(e)->IsActive()) && ...))
                    {
                        continue;
                    }
                    m_Awake(e, *m_pEcs->GetComponent<T>(e)...);
                }
            }
        }
        void InitializeEntity(Entity e) override
        {
            if (!m_Init)
            {
                return;
            }
            // ステージングも含めて、必要なコンポーネントが存在＆アクティブかチェック
            if (!((m_pEcs->GetComponent<T>(e) && m_pEcs->GetComponent<T>(e)->IsActive()) && ...))
            {
                return;
            }
            // 初期化コールバックを実行
            m_Init(e, *m_pEcs->GetComponent<T>(e)...);
        }
        void FinalizeEntity(Entity e) override
        {
            if (!m_Fin)
            {
                return;
            }
            // ステージングも含めて、必要なコンポーネントが存在＆アクティブかチェック
            if (!((m_pEcs->GetComponent<T>(e) && m_pEcs->GetComponent<T>(e)->IsActive()) && ...))
            {
                return;
            }
            // 終了コールバック
            m_Fin(e, *m_pEcs->GetComponent<T>(e)...);
        }
        void AwakeEntity(Entity e) override
        {
            if (!m_Awake)
            {
                return;
            }
            // ステージングも含めて、必要なコンポーネントが存在＆アクティブかチェック
            if (!((m_pEcs->GetComponent<T>(e) && m_pEcs->GetComponent<T>(e)->IsActive()) && ...))
            {
                return;
            }
            // Awakeコールバック
            m_Awake(e, *m_pEcs->GetComponent<T>(e)...);
        }
        const Archetype &GetRequired() const { return m_Required; }

      private:
        Archetype m_Required;
        UpdateFunc m_Update;
        InitFunc m_Init;
        FinFunc m_Fin;
        AwakeFunc m_Awake; // 追加：Awake用のコールバック
    };

    template <ComponentType T>
        requires IsMultiComponent<T>::value
    class MultiComponentSystem : public ISystem
    {
        using InitFunc = std::function<void(Entity, std::vector<T> &)>;
        using UpdateFunc = std::function<void(Entity, std::vector<T> &)>;
        using FinFunc = std::function<void(Entity, std::vector<T> &)>;
        using AwakeFunc = std::function<void(Entity, std::vector<T> &)>;

      public:
        explicit MultiComponentSystem(
            UpdateFunc u,
            InitFunc i = {},
            FinFunc f = {},
            AwakeFunc w = {})
            : m_Update(u), m_Init(i), m_Fin(f), m_Awake(w)
        {
        }
        // 起動時に一度だけ呼ばれる
        void Initialize() override
        {
            if (!m_Init)
            {
                return;
            }
            processAll(m_Init);
        }

        // 毎フレーム呼ばれる
        void Update() override
        {
            processAll(m_Update);
        }

        // 終了時に一度だけ呼ばれる
        void Finalize() override
        {
            if (!m_Fin)
            {
                return;
            }
            processAll(m_Fin);
        }

        // Awake フェーズでエンティティごとの処理
        void Awake() override
        {
            if (!m_Awake)
            {
                return;
            }
            processAll(m_Awake);
        }
        void InitializeEntity(Entity e) override
        {
            if (!m_Init)
            {
                return;
            }
            auto *pool = m_pEcs->GetComponentPool<T>();
            if (!pool)
            {
                return;
            }

            std::vector<T> filtered;

            // ① ステージングバッファのチェック
            const auto &stagingMap = pool->GetStagingMulti(); // m_StagingMulti への参照を返す
            if (auto itS = stagingMap.find(e); itS != stagingMap.end())
            {
                for (auto &inst : itS->second)
                {
                    if (inst.IsActive())
                    {
                        filtered.push_back(inst);
                    }
                }
            }

            // ② 本番データのチェック
            const auto &mainMap = pool->Map(); // 既存の m_Multi
            if (auto itM = mainMap.find(e); itM != mainMap.end())
            {
                for (auto &inst : itM->second)
                {
                    if (inst.IsActive())
                    {
                        filtered.push_back(inst);
                    }
                }
            }

            if (!filtered.empty())
            {
                m_Init(e, filtered);
            }
        }
        void FinalizeEntity(Entity e) override
        {
            if (!m_Fin)
            {
                return;
            }
            auto *pool = m_pEcs->GetComponentPool<T>();
            if (!pool)
            {
                return;
            }
            // 本番バッファのみ（ステージング反映後にここが呼ばれる想定）
            auto it = pool->Map().find(e);
            if (it == pool->Map().end() || it->second.empty())
            {
                return;
            }
            // 有効なものだけを抜き出して呼ぶ
            std::vector<T> tmp;
            for (auto &c : it->second)
            {
                if (c.IsActive())
                {
                    tmp.push_back(c);
                }
            }
            if (!tmp.empty())
            {
                m_Fin(e, tmp);
            }
        }
        void AwakeEntity(Entity e) override
        {
            if (!m_Awake)
            {
                return;
            }
            auto *pool = m_pEcs->GetComponentPool<T>();
            if (!pool)
            {
                return;
            }

            std::vector<T> filtered;

            // ① ステージングバッファのチェック
            const auto &stagingMap = pool->GetStagingMulti(); // m_StagingMulti への参照を返す
            if (auto itS = stagingMap.find(e); itS != stagingMap.end())
            {
                for (auto &inst : itS->second)
                {
                    if (inst.IsActive())
                    {
                        filtered.push_back(inst);
                    }
                }
            }

            // ② 本番データのチェック
            const auto &mainMap = pool->Map(); // 既存の m_Multi
            if (auto itM = mainMap.find(e); itM != mainMap.end())
            {
                for (auto &inst : itM->second)
                {
                    if (inst.IsActive())
                    {
                        filtered.push_back(inst);
                    }
                }
            }

            if (!filtered.empty())
            {
                m_Awake(e, filtered);
            }
        }

      private:
        // 実際にプールを走査してコールバックを呼び出す共通処理
        template <typename Func>
        void processAll(const Func &func)
        {
            auto *pool = m_pEcs->GetComponentPool<T>();
            if (!pool)
            {
                return;
            }

            for (auto &[e, vec] : pool->Map())
            {
                // 1) エンティティがアクティブでない、またはインスタンスが空ならスキップ
                if (!m_pEcs->IsEntityActive(e) || vec.empty())
                {
                    continue;
                }

                // 2) インスタンスごとに IsActive フラグを確認して、filteredVec を作る
                std::vector<T> filtered;
                filtered.reserve(vec.size());
                for (auto &inst : vec)
                {
                    if (inst.IsActive())
                    {
                        filtered.push_back(inst);
                    }
                }

                // 3) 有効インスタンスがひとつでもあればコール
                if (!filtered.empty())
                {
                    func(e, filtered);
                }
            }
        }

        UpdateFunc m_Update;
        InitFunc m_Init;
        FinFunc m_Fin;
        AwakeFunc m_Awake; // 追加：Awake用のコールバック
    };

    std::unordered_map<Archetype, EntityContainer> &GetArchToEntities() { return m_ArchToEntities; }

  private:
    [[nodiscard]] Math::TimeSpan capture_now_() const noexcept
    {
        if (m_Clock == nullptr)
        {
            return Math::TimeSpan::zero();
        }

        return m_Clock->now_ns();
    }

    [[nodiscard]] double elapsed_ms_(Math::TimeSpan a_begin, Math::TimeSpan a_end) const noexcept
    {
        return (a_end - a_begin).ms_f64();
    }

    void sort_systems_by_priority_()
    {
        std::sort(
            m_Systems.begin(),
            m_Systems.end(),
            [](const auto &a_left, const auto &a_right)
            {
                return a_left->GetPriority() < a_right->GetPriority();
            });
    }

    void NotifyComponentAdded(Entity e, CompID c)
    {
        for (auto &wp : m_ComponentListeners)
        {
            if (auto sp = wp.lock())
            {
                sp->OnComponentAdded(e, c); // Notify listeners
            }
        }
    }
    void NotifyComponentCopied(Entity src, Entity dst, CompID c)
    {
        for (auto &wp : m_ComponentListeners)
        {
            if (auto sp = wp.lock())
            {
                sp->OnComponentCopied(src, dst, c); // Notify listeners
            }
        }
    }
    void NotifyComponentRemoved(Entity e, CompID c)
    {
        for (auto &wp : m_ComponentListeners)
        {
            if (auto sp = wp.lock())
            {
                sp->OnComponentRemoved(e, c);
            }
        }
    }
    void NotifyComponentRemovedInstance(Entity e, CompID c, void *v, size_t i)
    {
        for (auto &wp : m_ComponentListeners)
        {
            if (auto sp = wp.lock())
            {
                sp->OnComponentRemovedInstance(e, c, v, i);
            }
        }
    }
    void NotifyComponentRestoredFromPrefab(Entity e, CompID c)
    {
        for (auto &wp : m_ComponentListeners)
        {
            if (auto sp = wp.lock())
            {
                sp->OnComponentRestoredFromPrefab(e, c);
            }
        }
    }
    bool IsMultiComponentByID(CompID id) const
    {
        auto it = m_TypeToComponents.find(id);
        if (it != m_TypeToComponents.end())
        {
            return it->second->IsMultiComponentTrait(id);
        }
        return false;
    }
    // ――――――――――――――――――
    //  RemoveEntity の本体（ClearEntity → リスナ通知 → 再利用キュー）
    // ――――――――――――――――――
    void RemoveEntityImpl(Entity e)
    {
        // 1) すべてのコンポーネントをクリア（イベント込み）
        ClearEntity(e);
        // 2) 非アクティブ化
        m_EntityToActive[e] = false;
        // 3) リサイクル
        m_RecycleEntities.push_back(e);
        // 4) エンティティ破棄イベント
        for (auto &wp : m_EntityListeners)
        {
            if (auto sp = wp.lock())
            {
                sp->OnEntityDestroyed(e);
            }
        }
    }

    // フレーム末にまとめてコマンドを実行
    void FlushDeferred()
    {
        for (auto &cmd : m_DeferredCommands)
        {
            cmd();
        }
        m_DeferredCommands.clear();
    }

    /*-------------------- data members --------------------------------*/

    Core::Time::IClock *m_Clock = nullptr;
    bool m_IsUpdating = false;
    bool m_CancelUpdate = false;
    Entity m_NextEntityID = static_cast<Entity>(-1);
    std::vector<bool> m_EntityToActive;
    std::vector<Entity> m_RecycleEntities;
    std::vector<Entity> m_StagingEntities;            // フレーム中に生成された Entity の一覧
    std::vector<bool> m_StagingEntityActive;          // 各 Entity の一時アクティブ状態
    std::vector<Archetype> m_StagingEntityArchetypes; // 各 Entity の一時 Archetype
    std::vector<Archetype> m_EntityToArchetype;
    std::unordered_map<CompID, int> m_DeletePriority; // デフォルトは 0 (CompID 昇順になる)
    std::unordered_map<CompID, int> m_CopyPriority;   // コピー時の優先度マップ
    std::vector<std::function<void()>> m_DeferredCommands;
    std::vector<std::weak_ptr<IEntityEventListener>> m_EntityListeners;
    std::vector<std::unique_ptr<ISystem>> m_Systems;
    std::vector<std::weak_ptr<IIComponentEventListener>> m_ComponentListeners;
    std::unordered_map<Archetype, EntityContainer> m_ArchToEntities;
    std::unordered_map<CompID, std::shared_ptr<IComponentPool>> m_TypeToComponents;
    double m_LastTotalUpdateTimeMs = 0.0;                                 // 最後に計測した全システム更新の所要時間（ms）
    std::unordered_map<std::type_index, double> m_LastSystemUpdateTimeMs; // 各システム型ごとに最後に計測した更新時間（ms）

    // Initialize／Finalize の計測用
    double m_LastTotalInitializeTimeMs = 0.0;
    double m_LastTotalFinalizeTimeMs = 0.0;
    double m_LastTotalAwakeTimeMs = 0.0;

    std::unordered_map<std::type_index, double> m_LastSystemInitializeTimeMs;
    std::unordered_map<std::type_index, double> m_LastSystemFinalizeTimeMs;
    std::unordered_map<std::type_index, double> m_LastSystemAwakeTimeMs;

    // SystemごとのUpdate直前に処理する初期化対象格納用
    std::unordered_map<std::type_index, std::vector<Entity>> m_PendingInitBeforeUpdate;
    // 新規に追加された Entity を記録するバッファ
    std::vector<Entity> m_NewEntitiesLastFrame;
};

struct IComponentEventListener : public IIComponentEventListener
{
    friend class ECSManager;

  public:
    IComponentEventListener() = default;
    virtual ~IComponentEventListener()
    {
        onAddSingle.clear();
    }

    // 依存する ECSManager のポインタをセット
    void SetECSManager(ECSManager *ecs) { m_pEcs = ecs; }

    // 単一用
    template <ComponentType T>
    void RegisterOnAdd(std::function<void(Entity, T *)> f)
    {
        static_assert(!IsMultiComponent<T>::value, "Use singleComponent");
        CompID id = ECSManager::ComponentPool<T>::GetID();
        onAddSingle[id].push_back(
            [f](Entity e, IComponentTag *raw)
            {
                f(e, static_cast<T *>(raw));
            });
    }
    // マルチ用
    template <ComponentType T>
    void RegisterOnAdd(std::function<void(Entity, T *, size_t)> f)
    {
        static_assert(IsMultiComponent<T>::value, "Use multiComponent");
        CompID id = ECSManager::ComponentPool<T>::GetID();
        onAddMulti[id].push_back(
            [f](Entity e, void *rawVec, size_t idx)
            {
                auto *vec = static_cast<std::vector<T> *>(rawVec);
                f(e, &(*vec)[idx], idx);
            });
    }

    // 単一コンポーネント用
    template <ComponentType T>
    void RegisterOnCopy(std::function<void(Entity, Entity, T *)> f)
    {
        static_assert(!IsMultiComponent<T>::value, "Use singleComponent");
        CompID id = ECSManager::ComponentPool<T>::GetID();
        onCopySingle[id].push_back(
            [f](Entity src, Entity dst, IComponentTag *raw)
            {
                f(src, dst, static_cast<T *>(raw));
            });
    }

    // マルチコンポーネント用
    template <ComponentType T>
    void RegisterOnCopy(std::function<void(Entity, Entity, T *, size_t)> f)
    {
        static_assert(IsMultiComponent<T>::value, "Use multiComponent");
        CompID id = ECSManager::ComponentPool<T>::GetID();
        onCopyMulti[id].push_back(
            [f](Entity src, Entity dst, void *rawVec, size_t idx)
            {
                auto *vec = static_cast<std::vector<T> *>(rawVec);
                f(src, dst, &(*vec)[idx], idx);
            });
    }

    // 単一コンポーネント用
    template <ComponentType T>
    void RegisterOnRemove(std::function<void(Entity, T *)> f)
    {
        static_assert(!IsMultiComponent<T>::value, "Use singleComponent");
        CompID id = ECSManager::ComponentPool<T>::GetID();
        onRemoveSingle[id].push_back(
            [f](Entity e, IComponentTag *raw)
            {
                f(e, static_cast<T *>(raw));
            });
    }

    // マルチコンポーネント用（インスタンスごとに index 付き）
    template <ComponentType T>
    void RegisterOnRemove(std::function<void(Entity, T *, size_t)> f)
    {
        static_assert(IsMultiComponent<T>::value, "Use multiComponent");
        CompID id = ECSManager::ComponentPool<T>::GetID();
        onRemoveMulti[id].push_back(
            [f](Entity e, void *rawVec, size_t idx)
            {
                auto *vec = static_cast<std::vector<T> *>(rawVec);
                f(e, &(*vec)[idx], idx);
            });
    }
    // 単一コンポーネント向け
    template <ComponentType T>
    void RegisterOnRestore(std::function<void(Entity, T *)> f)
    {
        static_assert(!IsMultiComponent<T>::value, "Use multi for multi-component");
        CompID id = ECSManager::ComponentPool<T>::GetID();
        onRestoreSingle[id].push_back(
            [f](Entity e, IComponentTag *raw)
            {
                f(e, static_cast<T *>(raw));
            });
    }

    // マルチコンポーネント向け
    template <ComponentType T>
    void RegisterOnRestore(std::function<void(Entity, T *, size_t)> f)
    {
        static_assert(IsMultiComponent<T>::value, "Use single for single-component");
        CompID id = ECSManager::ComponentPool<T>::GetID();
        onRestoreMulti[id].push_back(
            [f](Entity e, void *rawVec, size_t idx)
            {
                auto *vec = static_cast<std::vector<T> *>(rawVec);
                f(e, &(*vec)[idx], idx);
            });
    }

  private:
    // ECS側から呼ばれる
    void OnComponentAdded(Entity e, CompID compType) override
    {
        // ① 単一コンポーネント向け
        if (auto it = onAddSingle.find(compType); it != onAddSingle.end())
        {
            void *raw = m_pEcs->GetRawComponentPool(compType)->GetRawComponent(e);
            for (auto &cb : it->second)
            {
                cb(e, static_cast<IComponentTag *>(raw));
            }
        }

        // ② マルチコンポーネント向け
        if (auto it2 = onAddMulti.find(compType); it2 != onAddMulti.end())
        {
            ECSManager::IComponentPool *pool = m_pEcs->GetRawComponentPool(compType);
            void *rawVec = pool->GetRawComponent(e);
            size_t count = pool->GetComponentCount(e);

            // **新しく追加されたインスタンスの index = count-1**
            size_t idxNew = (count == 0 ? 0 : count - 1);

            for (auto &cb : it2->second)
            {
                cb(e, rawVec, idxNew);
            }
        }
    }
    void OnComponentCopied(Entity src, Entity dst, CompID compType) override
    {
        // 単一コンポーネント向け
        if (auto it = onCopySingle.find(compType); it != onCopySingle.end())
        {
            void *raw = m_pEcs->GetRawComponentPool(compType)->GetRawComponent(dst);
            for (auto &cb : it->second)
            {
                cb(src, dst, static_cast<IComponentTag *>(raw));
            }
        }

        // マルチコンポーネント向け
        if (auto it2 = onCopyMulti.find(compType); it2 != onCopyMulti.end())
        {
            ECSManager::IComponentPool *pool = m_pEcs->GetRawComponentPool(compType);
            void *rawVec = pool->GetRawComponent(dst);
            size_t count = pool->GetComponentCount(dst);

            for (size_t idx = 0; idx < count; ++idx)
            {
                for (auto &cb : it2->second)
                {
                    cb(src, dst, rawVec, idx);
                }
            }
        }
    }
    void OnComponentRemoved(Entity e, CompID compType) override
    {
        // ① 単一コンポーネント向け
        if (auto it = onRemoveSingle.find(compType); it != onRemoveSingle.end())
        {
            void *raw = m_pEcs->GetRawComponentPool(compType)
                            ->GetRawComponent(e);
            for (auto &cb : it->second)
            {
                cb(e, static_cast<IComponentTag *>(raw));
            }
        }

        // ② マルチコンポーネント向け
        if (auto it2 = onRemoveMulti.find(compType); it2 != onRemoveMulti.end())
        {
            ECSManager::IComponentPool *pool = m_pEcs->GetRawComponentPool(compType);
            void *rawVec = pool->GetRawComponent(e);
            size_t count = pool->GetComponentCount(e);

            for (size_t idx = 0; idx < count; ++idx)
            {
                for (auto &cb : it2->second)
                {
                    cb(e, rawVec, idx);
                }
            }
        }
    }
    void OnComponentRemovedInstance(Entity e, CompID compType, void *rawVec, size_t idx) override
    {
        if (auto it = onRemoveMulti.find(compType); it != onRemoveMulti.end())
        {
            for (auto &cb : it->second)
            {
                cb(e, rawVec, idx);
            }
        }
    }

    void OnComponentRestoredFromPrefab(Entity e, CompID compType) override
    {
        // 単一
        if (auto it = onRestoreSingle.find(compType); it != onRestoreSingle.end())
        {
            void *raw = m_pEcs->GetRawComponentPool(compType)->GetRawComponent(e);
            for (auto &cb : it->second)
            {
                cb(e, static_cast<IComponentTag *>(raw));
            }
        }
        // マルチ
        if (auto it2 = onRestoreMulti.find(compType); it2 != onRestoreMulti.end())
        {
            auto *pool = m_pEcs->GetRawComponentPool(compType);
            void *rawVec = pool->GetRawComponent(e);
            size_t count = pool->GetComponentCount(e);
            for (size_t idx = 0; idx < count; ++idx)
            {
                for (auto &cb : it2->second)
                {
                    cb(e, rawVec, idx);
                }
            }
        }
    }

  private:
    ECSManager *m_pEcs = nullptr;

  protected:
    // 単一用
    std::unordered_map<CompID, std::vector<std::function<void(Entity, IComponentTag *)>>> onAddSingle;
    // マルチ用
    std::unordered_map<CompID, std::vector<std::function<void(Entity, void *, size_t)>>> onAddMulti;
    // 単一用
    std::unordered_map<CompID, std::vector<std::function<void(Entity, Entity, IComponentTag *)>>> onCopySingle;
    // マルチ用
    std::unordered_map<CompID, std::vector<std::function<void(Entity, Entity, void *, size_t)>>> onCopyMulti;
    // 単一用
    std::unordered_map<CompID, std::vector<std::function<void(Entity, IComponentTag *)>>> onRemoveSingle;
    // マルチ用
    std::unordered_map<CompID, std::vector<std::function<void(Entity, void *, size_t)>>> onRemoveMulti;
    // Restore（Prefab 復元）用マップ
    std::unordered_map<CompID, std::vector<std::function<void(Entity, IComponentTag *)>>> onRestoreSingle;
    std::unordered_map<CompID, std::vector<std::function<void(Entity, void *, size_t)>>> onRestoreMulti;
};

class IPrefab
{
  public:
    IPrefab() = default;

    // ――――――――――――――――――
    //  ① 事前登録：扱う型すべてに対して呼ぶ
    //  Prefab::RegisterCopyFunc<YourComponent>();
    // ――――――――――――――――――
    template <ComponentType T>
    static void RegisterCopyFunc()
    {
        CompID id = ECSManager::ComponentPool<T>::GetID();

        if constexpr (IsMultiComponent<T>::value)
        {
            m_MultiCopyFuncs[id] = [](Entity e, ECSManager &ecs, void *rawVec)
            {
                auto *vec = static_cast<std::vector<T> *>(rawVec);
                for (auto const &comp : *vec)
                {
                    T *dst = ecs.AddComponent<T>(e);
                    *dst = comp;
                }
            };
        }
        else
        {
            m_CopyFuncs[id] = [](Entity e, ECSManager &ecs, void *raw)
            {
                T *dst = ecs.AddComponent<T>(e);
                *dst = *static_cast<T *>(raw);
            };
        }
    }

    template <ComponentType T>
    static void RegisterPrefabRestore()
    {
        CompID id = ECSManager::ComponentPool<T>::GetID();

        // 単一コンポーネント用
        m_PrefabRestoreFuncs[id] = [](Entity e, ECSManager &ecs, void *raw)
        {
            ecs.PrefabAddComponent<T>(e, *static_cast<T *>(raw));
        };

        // マルチコンポーネント用
        m_PrefabRestoreMultiFuncs[id] = [](Entity e, ECSManager &ecs, void *rawVec)
        {
            auto &vec = *static_cast<std::vector<T> *>(rawVec);
            for (auto &inst : vec)
            {
                ecs.PrefabAddComponent<T>(e, inst);
            }
        };
    }

    // ――――――――――――――――――
    //  ② 既存エンティティから Prefab を作る
    // ――――――――――――――――――
    static IPrefab FromEntity(ECSManager &ecs, Entity e)
    {
        IPrefab prefab;
        const Archetype &arch = ecs.GetArchetype(e);

        for (size_t id = 0; id < arch.size(); ++id)
        {
            if (!arch.test(id))
            {
                continue;
            }
            auto *pool = ecs.GetRawComponentPool(id);
            void *raw = pool->GetRawComponent(e);
            if (!raw)
            {
                continue;
            }

            // 深いコピーを shared_ptr<void> で受け取る
            auto clonePtr = pool->CloneComponent(id, raw);
            if (pool->IsMultiComponentTrait(id))
            {
                prefab.m_MultiComponents[id] = std::move(clonePtr);
            }
            else
            {
                prefab.m_Components[id] = std::move(clonePtr);
            }

            prefab.m_Archetype.set(id);
        }
        return prefab;
    }

    // ――――――――――――――――――
    //  ③ Instantiate するとき
    // ――――――――――――――――――
    Entity Instantiate(ECSManager &ecs) const
    {
        // 1) エンティティ生成（派生で名前付けしたい場合はここをオーバーライド）
        Entity e = CreateEntity(ecs);

        // 2) コンポーネント復元（派生は基本的にそのまま使う）
        InstantiateComponents(e, ecs);

        // 3) 子 Prefab 再帰生成＆親子リンク
        InstantiateChildren(e, ecs);

        // 4) 完了フック（後始末的な処理があれば派生で）
        OnAfterInstantiate(e, ecs);

        return e;
    }

    // ――――――――――――――――――
    //  ④ Prefab にコンポーネントを追加するとき
    // ――――――――――――――――――
    template <ComponentType T>
    void AddComponent(const T &comp)
    {
        CompID id = ECSManager::ComponentPool<T>::GetID();
        if constexpr (IsMultiComponent<T>::value)
        {
            auto &ptr = m_MultiComponents[id];
            if (!ptr)
            {
                ptr = std::make_shared<std::vector<T>>();
            }
            auto &vec = *std::static_pointer_cast<std::vector<T>>(ptr);
            vec.push_back(comp);
        }
        else
        {
            m_Components[id] = std::make_shared<T>(comp);
        }
        m_Archetype.set(id);
    }

    // ――――――――――――――――――
    //  ⑤ 子 Prefab を追加
    // ――――――――――――――――――
    void AddSubPrefab(std::shared_ptr<IPrefab> child)
    {
        m_SubPrefabs.push_back(std::move(child));
        // 必要なら m_Archetype にもビットを立てる
    }

    // ――――――――――――――――――
    //  ⑥ 子 Prefab 一覧取得
    // ――――――――――――――――――
    std::vector<std::shared_ptr<IPrefab>> &GetSubPrefabs() noexcept
    {
        return m_SubPrefabs;
    }

    const std::vector<std::shared_ptr<IPrefab>> &GetSubPrefabs() const noexcept
    {
        return m_SubPrefabs;
    }

    // ――――――――――――――――――
    //  コンポーネントを 1 つだけ持つ場合の参照取得
    // ――――――――――――――――――
    template <ComponentType T>
    T &GetComponent()
    {
        CompID id = ECSManager::ComponentPool<T>::GetID();
        auto it = m_Components.find(id);
        if (it == m_Components.end())
        {
            throw std::runtime_error("Prefab にそのコンポーネントはありません");
        }
        return *std::static_pointer_cast<T>(it->second);
    }

    // ――――――――――――――――――
    //  マルチコンポーネントをすべて参照
    // ――――――――――――――――――
    template <ComponentType T>
    std::vector<T> &GetAllComponents()
        requires IsMultiComponent<T>::value
    {
        CompID id = ECSManager::ComponentPool<T>::GetID();
        auto it = m_MultiComponents.find(id);
        if (it == m_MultiComponents.end())
        {
            throw std::runtime_error("Prefab にそのマルチコンポーネントはありません");
        }
        return *std::static_pointer_cast<std::vector<T>>(it->second);
    }

    // 単一コンポーネント取得：存在しなければ nullptr
    template <ComponentType T>
    T *GetComponentPtr() const noexcept
    {
        CompID id = ECSManager::ComponentPool<T>::GetID();
        auto it = m_Components.find(id);
        if (it == m_Components.end())
        {
            return nullptr;
        }
        // shared_ptr<void> を shared_ptr<T> にキャストして生ポインタを返す
        auto ptr = std::static_pointer_cast<T>(it->second);
        return ptr.get();
    }

    // マルチコンポーネント取得：存在しなければ nullptr
    template <ComponentType T>
    std::vector<T> *GetAllComponentsPtr() const noexcept
        requires IsMultiComponent<T>::value
    {
        CompID id = ECSManager::ComponentPool<T>::GetID();
        auto it = m_MultiComponents.find(id);
        if (it == m_MultiComponents.end())
        {
            return nullptr;
        }
        auto ptr = std::static_pointer_cast<std::vector<T>>(it->second);
        return ptr.get();
    }

    // ――――――――――――――――――
    //  単一コンポーネントを上書きする
    // ――――――――――――――――――
    template <ComponentType T>
    void SetComponent(const T &comp)
    {
        CompID id = ECSManager::ComponentPool<T>::GetID();
        m_Components[id] = std::make_shared<T>(comp);
        m_Archetype.set(id);
    }

    // ――――――――――――――――――
    //  単一コンポーネントを Prefab から削除
    // ――――――――――――――――――
    template <ComponentType T>
    void RemoveComponent()
    {
        CompID id = ECSManager::ComponentPool<T>::GetID();
        // マップから単一コンポーネントのエントリを消す
        m_Components.erase(id);
        // Archetype からビットをクリア
        m_Archetype.reset(id);
    }

    // ――――――――――――――――――
    //  マルチコンポーネントの特定インスタンスを Prefab から削除
    // ――――――――――――――――――
    template <ComponentType T>
    void RemoveComponentInstance(size_t idx)
        requires IsMultiComponent<T>::value
    {
        CompID id = ECSManager::ComponentPool<T>::GetID();
        auto it = m_MultiComponents.find(id);
        if (it == m_MultiComponents.end())
        {
            return;
        }

        auto &vec = *std::static_pointer_cast<std::vector<T>>(it->second);
        if (idx >= vec.size())
        {
            return;
        }
        vec.erase(vec.begin() + idx);

        // もし空になったら Prefab から完全に消す
        if (vec.empty())
        {
            m_MultiComponents.erase(id);
            m_Archetype.reset(id);
        }
    }

    // ――――――――――――――――――
    //  マルチコンポーネント全体を Prefab から削除
    // ――――――――――――――――――
    template <ComponentType T>
    void ClearAllComponents()
        requires IsMultiComponent<T>::value
    {
        CompID id = ECSManager::ComponentPool<T>::GetID();
        m_MultiComponents.erase(id);
        m_Archetype.reset(id);
    }

  protected:
    // —————— hooks ——————

    void PopulateFromEntity(ECSManager &ecs, Entity e)
    {
        const Archetype &arch = ecs.GetArchetype(e);

        for (size_t id = 0; id < arch.size(); ++id)
        {
            if (!arch.test(id))
            {
                continue;
            }
            auto *pool = ecs.GetRawComponentPool(id);
            void *raw = pool->GetRawComponent(e);
            if (!raw)
            {
                continue;
            }

            // 深いコピーを shared_ptr<void> で受け取る
            auto clonePtr = pool->CloneComponent(id, raw);
            if (pool->IsMultiComponentTrait(id))
            {
                m_MultiComponents[id] = std::move(clonePtr);
            }
            else
            {
                m_Components[id] = std::move(clonePtr);
            }

            m_Archetype.set(id);
        }
    }

    // (1) まずエンティティを作る。追加の初期化／名前付けは here を override。
    virtual Entity CreateEntity(ECSManager &ecs) const
    {
        return ecs.GenerateEntity();
    }

    // (2) m_Components/m_MultiComponents を使って既存の復元ロジック
    virtual void InstantiateComponents(Entity e, ECSManager &ecs) const
    {
        // 単一コンポーネント
        for (auto const &[id, rawPtr] : m_Components)
        {
            auto it = m_PrefabRestoreFuncs.find(id);
            if (it != m_PrefabRestoreFuncs.end())
            {
                it->second(e, ecs, rawPtr.get());
            }
        }

        // マルチコンポーネント
        for (auto const &[id, rawVec] : m_MultiComponents)
        {
            auto it = m_PrefabRestoreMultiFuncs.find(id);
            if (it != m_PrefabRestoreMultiFuncs.end())
            {
                it->second(e, ecs, rawVec.get());
            }
        }
    }

    // (3) 子 Prefab を再帰的にインスタンス化し、リンク用フックを呼ぶ
    virtual void InstantiateChildren(Entity parent, ECSManager &ecs) const
    {
        for (auto const &child : m_SubPrefabs)
        {
            Entity childEnt = child->Instantiate(ecs);
            LinkParentChild(parent, childEnt, ecs);
        }
    }

    // 親→子を結びつけたい場合はここを override
    virtual void LinkParentChild(Entity /*parent*/, Entity /*child*/, ECSManager & /*ecs*/) const
    {
        // default: なにもしない
    }

    // (4) 全部終わったあとに追加処理したい場合はここ
    virtual void OnAfterInstantiate(Entity /*e*/, ECSManager & /*ecs*/) const
    {
        // default: なにもしない
    }

  private:
    Archetype m_Archetype;
    // void ポインタで型消去したストレージ
    std::unordered_map<CompID, std::shared_ptr<void>> m_Components;
    std::unordered_map<CompID, std::shared_ptr<void>> m_MultiComponents;

    // 追加：ネストされた Prefab のリスト
    std::vector<std::shared_ptr<IPrefab>> m_SubPrefabs;

    // インスタンス化用マップ
    inline static std::unordered_map<CompID,
                                     std::function<void(Entity, ECSManager &, void *)>>
        m_CopyFuncs;
    inline static std::unordered_map<CompID,
                                     std::function<void(Entity, ECSManager &, void *)>>
        m_MultiCopyFuncs;
    // 復元処理マップ
    inline static std::unordered_map<CompID,
                                     std::function<void(Entity, ECSManager &, void *)>>
        m_PrefabRestoreFuncs;
    inline static std::unordered_map<CompID,
                                     std::function<void(Entity, ECSManager &, void *)>>
        m_PrefabRestoreMultiFuncs;
};
} // namespace Cue::ECS
