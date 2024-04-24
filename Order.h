#pragma once

#include <list>
#include <exception>
#include <format>

#include "OrderType.h"
#include "Side.h"
#include "Usings.h"
#include "Constants.h"

class Order
{
public:
    Order(OrderType orderType = OrderType::Market, OrderId orderId, Side side, Price price, Quantity quantity): 
        orderId_(orderId), side_(side), orderType_(orderType), price_(price), initialQuantity_(quantity), remainingQuantity_(quantity) {}
    
    OrderId getOrderId() const { return orderId_; }
    Side getSide() const { return side_; }
    Price getPrice() const { return price_; }
    OrderType getOrderType() const { return orderType_; }
    Quantity getInitialQuantity() const { return initialQuantity_; }
    Quantity getRemainingQuantity() const { return remainingQuantity_; }
    Quantity getFilledQuantity() const { return initialQuantity_ - remainingQuantity_; }
    bool isFilled() const { return remainingQuantity_ == 0; }

    void Fill(Quantity quantity)
    {
        if (quantity > remainingQuantity_)
        {
            throw std::logic_error(std::format("Order ({}) cannot be filled for more than its remaining quantity.", getOrderId()));
        }
        remainingQuantity_ -= quantity;
    }

    void toGoodTillCancel(Price price)
    {
        if (orderType_ != OrderType::Market)
        {
            throw std::logic_error(std::format("Order ({}) cannot be converted to GoodTillCancel, as it is ot a Market Order.", getOrderId()));
        }

        price_ = price;
        orderType_ = OrderType::GoodTillCancel;
    }

private:
    OrderId orderId_;
    Side side_;
    OrderType orderType_;
    Price price_;
    Quantity initialQuantity_;
    Quantity remainingQuantity_;
};

using OrderPointer = std::shared_ptr<Order>;
using OrderPointers = std::list<OrderPointer>;