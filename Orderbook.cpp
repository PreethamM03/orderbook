#include "Orderbook.h"

#include <numeric> 
#include <chrono>
#include <ctime>

void Orderbook::pruneGoodForDayOrders()
{
    using namespace std::chrono;
    const auto end = hours(16);

    while(true){
        const auto now = system_clock::now();
        const auto now_c = system_clock::to_time_t(now);
        std::tm now_parts;
        localtime_s(&now_parts, &now_c);

        if(now_parts.tm_hour >= end.count()){
            now_parts.tm_mday++;
        }

        now_parts.tm_hour = end.count();
        now_parts.tm_min = 0;
        now_parts.tm_sec = 0;

        auto next = system_clock::from_time_t(mktime(&now_parts));
        auto till = next - now + milliseconds(100);

        {
			std::unique_lock ordersLock{ ordersMutex_ };

			if (shutdown_.load(std::memory_order_acquire) ||
				shutdownConditionVariable_.wait_for(ordersLock, till) == std::cv_status::no_timeout)
				return;
		}

		OrdersIds orderIds;

		{
			std::scoped_lock ordersLock{ ordersMutex_ };

			for (const auto& [_, entry] : orders_)
			{
				const auto& [order, _] = entry;

				if (order->getOrderType() != OrderType::GoodForDay)
					continue;

				orderIds.push_back(order->getOrderId());
			}
		}

		cancelOrders(orderIds);


    }


}

void Orderbook::cancelOrders(OrdersIds orderIds)
{
    std::scoped_lock ordersLock{ ordersMutex_ };
    for(const auto& orderId : orderIds){
        cancelOrderInternal(orderId);
    }
}

void Orderbook::cancelOrderInternal(OrderId orderId)
{
    if (!orders_.contains(orderId))
        return;

    const auto [order, iterator] = orders_.at(orderId);
    orders_.erase(orderId);

    if(order->getSide() == Side::Sell)
    {
        auto price = order->getPrice();
		auto& orders = asks_.at(price);
		orders.erase(iterator);
		if (orders.empty())
			asks_.erase(price);
    }
    else
    {
        auto price = order->getPrice();
		auto& orders = bids_.at(price);
		orders.erase(iterator);
		if (orders.empty())
			bids_.erase(price);
    }
    onOrderCancelled(order);
}

void OrderBook::onOrderCancelled(OrderPointer order)
{
    updateLevelData(order->getPrice(), order->getRemainingQuantity(), LevelData::Action::Remove);
}

void OrderBook::onOrderAdded(OrderPointer order)
{
    updateLevelData(order->getPrice(), order->getInitialQuantity(), LevelData::Action::Add);
}

void OrderBook::onOrderMatched(Price price, Quantity quantity, bool isFullyFilled)
{
    updateLevelData(price, quantity, isFullyFilled ? LevelData::Action::Remove : LevelData::Action::Match);
}

void OrderBook::updateLevelData(Price price, Quanitity quantity, LevelData::Action action)
{
    auto& levelData = data_[price];
    switch(action)
    {
        case LevelData::Action::Add:
            levelData.quantity_ += quantity;
            levelData.count_++;
            break;
        case LevelData::Action::Remove:
            levelData.quantity_ -= quantity;
            levelData.count_--;
            break;
        case LevelData::Action::Match:
            levelData.quantity_ -= quantity;
            break;
    }

    if(levelData.quantity_ == 0)
        data_.erase(price);
} 

void OrderBook::canMatch(Side side, Price price)
{
    if(side==Side::Buy)
    {
        if(asks_.empty())
            return false;
        const auto& [bestAsk, _] = *asks_.begin();
        return price >= bestAsk;
    }
    else
    {
        if(bids_.empty())
            return false;
        const auto& [bestBid, _] = *bids_.begin();
        return price <= bestBid;
    }
}

bool OrderBook::canFullyFill(Side side, Price price, Quantity quantity) const
{
    if (!canMatch(side, price))
    {
        return false;
    }

	std::optional<Price> threshold;

	if (side == Side::Buy)
	{
		const auto [askPrice, _] = *asks_.begin();
		threshold = askPrice;
	}
	else
	{
		const auto [bidPrice, _] = *bids_.begin();
		threshold = bidPrice;
	}

	for (const auto& [levelPrice, levelData] : data_)
	{
		if (threshold.has_value() &&
			(side == Side::Buy && threshold.value() > levelPrice) ||
			(side == Side::Sell && threshold.value() < levelPrice))
        {
            continue;
        }

		if ((side == Side::Buy && levelPrice > price) ||
			(side == Side::Sell && levelPrice < price))
        {
            continue;
        }

		if (quantity <= levelData.quantity_)
        {
            return true
        }
		quantity -= levelData.quantity_;
	}
	return false;
}

Trades OrderBook::addOrder(OrderPointer order)
{
    std::scoped_lock ordersLock{ ordersMutex_ };

    if(orders_.contains(order->getOrderId()))
    {
        return {};
    }
    if (order->getOrderType() == OrderType::FillAndKill && !canMatch(order->getSide(), order->getPrice()))
		return { };

	if (order->getOrderType() == OrderType::FillOrKill && !canFullyFill(order->getSide(), order->getPrice(), order->getInitialQuantity()))
		return { };

    if (order->getOrderType() == OrderType::Market)
	{
		if (order->getSide() == Side::Buy && !asks_.empty())
		{
			const auto& [worstAsk, _] = *asks_.rbegin();
			order->toGoodTillCancel(worstAsk);
		}
		else if (order->getSide() == Side::Sell && !bids_.empty())
		{
			const auto& [worstBid, _] = *bids_.rbegin();
			order->toGoodTillCancel(worstBid);
		}
		else
        {
            return {};
        }
	}

    if (order->getSide() == Side::Buy)
	{
		auto& orders = bids_[order->getPrice()];
		orders.push_back(order);
		iterator = std::prev(orders.end());
	}
	else
	{
		auto& orders = asks_[order->getPrice()];
		orders.push_back(order);
		iterator = std::prev(orders.end());
	}

	orders_.insert({ order->getOrderId(), OrderEntry{ order, iterator } });
	
	onOrderAdded(order);
	
	return matchOrders();

}

void Orderbook::cancelOrder(OrderId orderId)
{
	std::scoped_lock ordersLock{ ordersMutex_ };

	cancelOrderInternal(orderId);
}

Trades Orderbook::ModifyOrder(OrderModify order)
{
	OrderType orderType;

	{
		std::scoped_lock ordersLock{ ordersMutex_ };

		if (!orders_.contains(order.getOrderId()))
			return { };

		const auto& [existingOrder, _] = orders_.at(order.getOrderId());
		orderType = existingOrder->getOrderType();
	}

	cancelOrder(order.getOrderId());
	return addOrder(order.toOrderPointer(orderType));
}

std::size_t Orderbook::Size() const
{
	std::scoped_lock ordersLock{ ordersMutex_ };
	return orders_.size(); 
}

OrderbookLevelInfos Orderbook::getOrderInfos() const
{
	LevelInfos bidInfos, askInfos;
	bidInfos.reserve(orders_.size());
	askInfos.reserve(orders_.size());

	auto CreateLevelInfos = [](Price price, const OrderPointers& orders)
	{
		return LevelInfo{ price, std::accumulate(orders.begin(), orders.end(), (Quantity)0,
			[](Quantity runningSum, const OrderPointer& order)
			{ return runningSum + order->getRemainingQuantity(); }) };
	};

	for (const auto& [price, orders] : bids_)
		bidInfos.push_back(createLevelInfos(price, orders));

	for (const auto& [price, orders] : asks_)
		askInfos.push_back(createLevelInfos(price, orders));

	return OrderbookLevelInfos{ bidInfos, askInfos };

}

Trades Orderbook::MatchOrders()
{
	Trades trades;
	trades.reserve(orders_.size());

	while (true)
	{
		if (bids_.empty() || asks_.empty())
			break;

		auto& [bidPrice, bids] = *bids_.begin();
		auto& [askPrice, asks] = *asks_.begin();

		if (bidPrice < askPrice)
        {
            break;
        }

		while (!bids.empty() && !asks.empty())
		{
			auto bid = bids.front();
			auto ask = asks.front();

			Quantity quantity = std::min(bid->getRemainingQuantity(), ask->getRemainingQuantity());

			bid->fill(quantity);
			ask->fill(quantity);

			if (bid->isFilled())
			{
				bids.pop_front();
				orders_.erase(bid->GetOrderId());
			}

			if (ask->isFilled())
			{
				asks.pop_front();
				orders_.erase(ask->GetOrderId());
			}


			trades.push_back(Trade{
				TradeInfo{ bid->GetOrderId(), bid->GetPrice(), quantity },
				TradeInfo{ ask->GetOrderId(), ask->GetPrice(), quantity } 
				});

			onOrderMatched(bid->GetPrice(), quantity, bid->IsFilled());
			onOrderMatched(ask->GetPrice(), quantity, ask->IsFilled());
		}

        if (bids.empty())
        {
            bids_.erase(bidPrice);
            data_.erase(bidPrice);
        }

        if (asks.empty())
        {
            asks_.erase(askPrice);
            data_.erase(askPrice);
        }
	}

	if (!bids_.empty())
	{
		auto& [_, bids] = *bids_.begin();
		auto& order = bids.front();
		if (order->getOrderType() == OrderType::FillAndKill)
			cancelOrder(order->getOrderId());
	}

	if (!asks_.empty())
	{
		auto& [_, asks] = *asks_.begin();
		auto& order = asks.front();
		if (order->getOrderType() == OrderType::FillAndKill)
			cancelOrder(order->getOrderId());
	}

	return trades;
}